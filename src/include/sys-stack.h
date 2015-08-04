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
**  Summary: REBOL Stack Definitions
**  Module:  sys-stack.h
**  Author:  Carl Sassenrath
**  Notes:
**
**	DSP: index to the top of stack (active value)
**	DSF: index to the base of stack frame (return value)
**
**	Stack frame format:
**
**		   +---------------+
**	DSF->0:| Return Value  | normally becomes TOS after func return
**		   +---------------+
**		 1:|  Prior Frame  | old DSF, block, and block index
**		   +---------------+
**		 2:|   Func Word   | for backtrace info
**		   +---------------+
**		 3:|   Func Value  | in case value is moved or modified
**		   +---------------+
**		 4:|     Arg 1     | args begin here
**		   +---------------+
**		   |     Arg 2     |
**		   +---------------+
**
***********************************************************************/


/***********************************************************************
**
**	REBOL DATA STACK (DS)
**
**		The data stack is mostly for REDUCE and COMPOSE, which use it
**		as a common buffer for values that are being gathered to be
**		inserted into another series.  It's better to go through this
**		buffer step because it means the precise size of the new
**		insertions are known ahead of time.  If a series is created,
**		it will not waste space or time on expansion, and if a series
**		is to be inserted into as a target, the proper size gap for
**		the insertion can be opened up exactly once (without any
**		need for repeatedly shuffling on individual insertions).
**
**		Beyond that purpose, the data stack can also be used as a
**		place to store a value to protect it from the garbage
**		collector.  The stack must be balanced in the case of success
**		when a native or action runs, but if a Trap() is called then
**		the stack will be automatically balanced.
**
**		The data stack specifically needs contiguous memory for its
**		applications.  That is more important than having stability
**		of pointers to any data on the stack.  Hence if any push or
**		pops can happen, there is no guarantee that the pointers will
**		remain consistent...as the memory buffer may need to be
**		reallocated (and hence relocated).  The index positions will
**		remain consistent, however: and using DSP and DS_AT it is
**		possible to work with stack items by index.
**
***********************************************************************/

// (D)ata (S)tack "(P)ointer" is an integer index into Rebol's data stack
#define DSP \
	cast(REBINT, SERIES_TAIL(DS_Series) - 1)

// Access value at given stack location
#define DS_AT(d) \
	BLK_SKIP(DS_Series, (d))

// Most recently pushed item, crashes Debug build if stack is empty, but is
// still an expression-like macro
#ifdef NDEBUG
	#define DS_TOP \
		BLK_LAST(DS_Series)
#else
	#define DS_TOP \
		( \
			SERIES_TAIL(DS_Series) == 0 \
				? *cast(REBVAL**, NULL) \
				: BLK_LAST(DS_Series) \
		)
#endif

#if !defined(NDEBUG)
	#define IN_DATA_STACK(p) \
		(SERIES_TAIL(DS_Series) != 0 && (p) >= DS_AT(0) && (p) <= DS_TOP)
#endif

// PUSHING: Note the DS_PUSH macros inherit the property of SET_XXX that
// they use their parameters multiple times.  Don't use with the result of
// a function call because that function could be called multiple times.
//
// If you push "unsafe" trash to the stack, it has the benefit of costing
// nothing extra in a release build for setting the value (as it is just
// left uninitialized).  But you must make sure that a GC can't run before
// you have put a valid value into the slot you pushed.
//
// Unsafe trash partially inlines Alloc_Tail_Blk, so it only pays for the
// function call in cases where expansion is necessary (rare case, as the
// data stack is preallocated and increments in chunks).
//
// !!! Currently we Panic instead of expanding, which will be changed once
// call frames have their own stack.

#define DS_PUSH_TRASH \
	( \
		SERIES_FITS(DS_Series, 1) \
			? cast(void, ++DS_Series->tail) \
			: ( \
				SERIES_REST(DS_Series) >= STACK_LIMIT \
					? Trap(RE_STACK_OVERFLOW) \
					: cast(void, cast(REBUPT, Alloc_Tail_Blk(DS_Series))) \
			), \
		SET_TRASH(DS_TOP) \
	)

#define DS_PUSH_TRASH_SAFE \
	(DS_PUSH_TRASH, SET_TRASH_SAFE(DS_TOP), VOID)

#define DS_PUSH(v) \
	(DS_PUSH_TRASH, *DS_TOP = *(v), VOID)

#define DS_PUSH_UNSET \
	(DS_PUSH_TRASH, SET_UNSET(DS_TOP), VOID)

#define DS_PUSH_NONE \
	(DS_PUSH_TRASH, SET_NONE(DS_TOP), VOID)

#define DS_PUSH_TRUE \
	(DS_PUSH_TRASH, SET_TRUE(DS_TOP), VOID)

#define DS_PUSH_INTEGER(n) \
	(DS_PUSH_TRASH, SET_INTEGER(DS_TOP, (n)), VOID)

#define DS_PUSH_DECIMAL(n) \
	(DS_PUSH_TRASH, SET_DECIMAL(DS_TOP, (n)), VOID)

// POPPING AND "DROPPING"

#define DS_DROP \
	(--DS_Series->tail, BLK_TERM(DS_Series), VOID)

#define DS_POP_INTO(v) \
	do { \
		assert(!IS_TRASH(DS_TOP) || VAL_TRASH_SAFE(DS_TOP)); \
		*(v) = *DS_TOP; \
		DS_DROP; \
	} while (0)

#ifdef NDEBUG
	#define DS_DROP_TO(dsp) \
		(DS_Series->tail = (dsp) + 1, VOID)
#else
	#define DS_DROP_TO(dsp) \
		do { \
			assert(DSP >= (dsp)); \
			while (DSP != (dsp)) {DS_DROP;} \
		} while (0)
#endif


/***********************************************************************
**
**	REBOL CALL STACK (CS)
**
**		The requirements for the call stack are different from the data
**		stack, due to a need for pointer stability.  Being an ordinary
**		series, the data stack will relocate its memory on expansion.
**		This creates problems for natives and actions where pointers to
**		parameters are saved to variables from D_ARG(N) macros.  These
**		would need a refresh after every potential expanding operation.
**
**		Having a separate data structure offers other opportunities,
**		such as hybridizing with CLOSURE! argument objects such that
**		they would not need to be copied from the data stack.  It also
**		allows freeing the information tracked by calls from the rule
**		of being strictly a sequence of REBVALs.
**
***********************************************************************/

struct Reb_Call {
	struct Reb_Call *prior;

	// In the Debug build, we make sure SET_DSF has happened on a call frame.
	// This way "pending" frames that haven't had their arguments fulfilled
	// can be checked to be sure no one tries to Get_Var out of them yet.
#if !defined(NDEBUG)
	REBOOL pending;
#endif

	REBCNT num_vars;	// !!! Redundant with VAL_FUNC_NUM_WORDS()?

	REBVAL *out;		// where to write the function's output

	REBVAL func;			// copy (important!!) of function for call

	REBVAL where;			// block and index of execution
	REBVAL label;			// func word backtrace

	REBVAL return_func;		// dynamic scoped return (coming soon!)

	// these are "variables"...SELF, RETURN, args, locals
	REBVAL vars[1];		// (array exceeds struct, but cannot be [0] in C++)
};

// !!! DSF is to be renamed (C)all (S)tack (P)ointer, but being left as DSF
// in the initial commit to try and cut back on the disruption seen in
// one commit, as there are already a lot of changes.
//
// Is the pointer to the topmost Rebol call frame, currently a naive singly
// linked list implementation, to be enhanced with a chunking method that
// does not require an Alloc_Mem call on each create.

#define DSF (CS_Running + 0) // avoid assignment to DSF via + 0

#ifdef NDEBUG
	#define SET_DSF(c) \
		(CS_Running = (c), VOID)
#else
	// In a "stress checked" debug mode, every time the DSF is accessed we
	// can verify that it is well-formed.
	#ifdef STRESS
		#define DSF (DSF_Stress())
	#endif

	#define SET_DSF(c) \
		( \
			CS_Running = (c), \
			(c) ? cast(void, (c)->pending = FALSE) : VOID \
		)
#endif

#define DSF_OUT(c)		((c)->out)
#define PRIOR_DSF(c)	((c)->prior)
#define DSF_WHERE(c)	c_cast(const REBVAL*, &(c)->where)
#define DSF_LABEL(c)	c_cast(const REBVAL*, &(c)->label)
#define DSF_FUNC(c)		c_cast(const REBVAL*, &(c)->func)
#define DSF_RETURN(c)	coming@soon

// VARS includes (*will* include) RETURN dispatching value, locals...
#define DSF_VAR(c,n)	(&(c)->vars[(n) - 1])
#define DSF_NUM_VARS(c)	((c)->num_vars)

// ARGS is the parameters and refinements
#define DSF_ARG(c,n)	DSF_VAR((c), (n) - 1 + FIRST_PARAM_INDEX)
#define DSF_NUM_ARGS(c)	(DSF_NUM_VARS(c) - (FIRST_PARAM_INDEX - 1))

// !!! The function spec numbers words according to their position.  0 is
// SELF, 1 is the return, 2 is the first argument.  This layout is in flux
// as the workings of locals are rethought...their most sensible location
// would be before the arguments as well.

// Reference from ds that points to current return value:
#define D_OUT			DSF_OUT(call_)
#define D_ARG(n)		DSF_ARG(call_, (n))
#define D_REF(n)		(!IS_NONE(D_ARG(n)))

#define DS_ARGC			DSF_NUM_ARGS(call_)
