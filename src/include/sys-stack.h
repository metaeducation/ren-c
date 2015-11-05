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
**  Notes:
**
**	This contains the implementations of two important stacks in
**	the evaluator: the Data Stack and the Call Stack
**
**	DATA STACK (CS_*):
**
**	The data stack is mostly for REDUCE and COMPOSE, which use it
**	as a common buffer for values that are being gathered to be
**	inserted into another series.  It's better to go through this
**	buffer step because it means the precise size of the new
**	insertions are known ahead of time.  If a series is created,
**	it will not waste space or time on expansion, and if a series
**	is to be inserted into as a target, the proper size gap for
**	the insertion can be opened up exactly once (without any
**	need for repeatedly shuffling on individual insertions).
**
**	Beyond that purpose, the data stack can also be used as a
**	place to store a value to protect it from the garbage
**	collector.  The stack must be balanced in the case of success
**	when a native or action runs.  But if `raise` is used to trigger
**	an error, then the stack will be automatically balanced in
**	the trap handling.
**
**	The data stack specifically needs contiguous memory for its
**	applications.  That is more important than having stability
**	of pointers to any data on the stack.  Hence if any push or
**	pops can happen, there is no guarantee that the pointers will
**	remain consistent...as the memory buffer may need to be
**	reallocated (and hence relocated).  The index positions will
**	remain consistent, however: and using DSP and DS_AT it is
**	possible to work with stack items by index.
**
**	CALL STACK (CS_*):
**
**	The requirements for the call stack are different from the data
**	stack, due to a need for pointer stability.  Being an ordinary
**	series, the data stack will relocate its memory on expansion.
**	This creates problems for natives and actions where pointers to
**	parameters are saved to variables from D_ARG(N) macros.  These
**	would need a refresh after every potential expanding operation.
**
**	Having a separate data structure offers other opportunities,
**	such as hybridizing with CLOSURE! argument objects such that
**	they would not need to be copied from the data stack.  It also
**	allows freeing the information tracked by calls from the rule
**	of being strictly a sequence of REBVALs.
**
***********************************************************************/


/***********************************************************************
**
**	At the moment, the data stack is *mostly* implemented as a typical
**	series.  Pushing unfilled slots on the stack (via PUSH_TRASH_UNSAFE)
**	partially inlines Alloc_Tail_List, so it only pays for the function
**	call in cases where expansion is necessary.
**
**	When Rebol was first open-sourced, there were other deviations from
**	being a normal series.  It was not terminated with a REB_END, so
**	you would be required to call a special DS_TERMINATE() routine to
**	put the terminator in place before using the data stack with a
**	routine that expected termination.  It also had to be expanded
**	manually, so a DS_PUSH was not guaranteed to trigger a potential
**	growth of the stack--if expansion hadn't been anticipated with a
**	large enough space for that push, it would corrupt memory.
**
**	Overall, optimizing the stack structure should be easier now that
**	it has a more dedicated purpose.  So those tricks are not being
**	used for the moment.  Future profiling can try those and other
**	approaches when a stable and complete system has been achieved.
**
***********************************************************************/

// (D)ata (S)tack "(P)ointer" is an integer index into Rebol's data stack
#define DSP \
	cast(REBINT, SERIES_TAIL(DS_Series) - 1)

// Access value at given stack location
#define DS_AT(d) \
	BLK_SKIP(DS_Series, (d))

// Most recently pushed item
#define DS_TOP \
	BLK_LAST(DS_Series)

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

#define DS_PUSH_TRASH \
	( \
		SERIES_FITS(DS_Series, 1) \
			? cast(void, ++DS_Series->tail) \
			: ( \
				SERIES_REST(DS_Series) >= STACK_LIMIT \
					? Trap_Stack_Overflow() \
					: cast(void, cast(REBUPT, Alloc_Tail_Array(DS_Series))) \
			), \
		SET_TRASH(DS_TOP) \
	)

#define DS_PUSH_TRASH_SAFE \
	(DS_PUSH_TRASH, SET_TRASH_SAFE(DS_TOP), NOOP)

#define DS_PUSH(v) \
	(ASSERT_VALUE_MANAGED(v), DS_PUSH_TRASH, *DS_TOP = *(v), NOOP)

#define DS_PUSH_UNSET \
	(DS_PUSH_TRASH, SET_UNSET(DS_TOP), NOOP)

#define DS_PUSH_NONE \
	(DS_PUSH_TRASH, SET_NONE(DS_TOP), NOOP)

#define DS_PUSH_TRUE \
	(DS_PUSH_TRASH, SET_TRUE(DS_TOP), NOOP)

#define DS_PUSH_INTEGER(n) \
	(DS_PUSH_TRASH, SET_INTEGER(DS_TOP, (n)), NOOP)

#define DS_PUSH_DECIMAL(n) \
	(DS_PUSH_TRASH, SET_DECIMAL(DS_TOP, (n)), NOOP)

// POPPING AND "DROPPING"

#define DS_DROP \
	(--DS_Series->tail, SET_END(BLK_TAIL(DS_Series)), NOOP)

#define DS_POP_INTO(v) \
	do { \
		assert(!IS_TRASH(DS_TOP) || VAL_TRASH_SAFE(DS_TOP)); \
		*(v) = *DS_TOP; \
		DS_DROP; \
	} while (0)

#ifdef NDEBUG
	#define DS_DROP_TO(dsp) \
		(DS_Series->tail = (dsp) + 1, SET_END(BLK_TAIL(DS_Series)), NOOP)
#else
	#define DS_DROP_TO(dsp) \
		do { \
			assert(DSP >= (dsp)); \
			while (DSP != (dsp)) {DS_DROP;} \
		} while (0)
#endif


/***********************************************************************
**
**	The call stack uses a custom "chunked" allocator to avoid the
**	overhead of calling Make_Mem on each push and Free_Mem on
**	each pop.  It keeps one spare chunk allocated, and only frees
**	a chunk when a full chunk prior to it has the last element
**	popped out of it.  In memory the situation looks like this:
**
**		[chunk->next
**			(->chunk_left call->prior ...data [arg1][arg2][arg3]...)
**			(->chunk_left call->prior ...data [arg1]...)
**			(->chunk_left call->prior ...data [arg1][arg2]...)
**			...chunk remaining space...
**		]
**
**	Each [chunk] contains (calls).  The calls are singly linked
**	backwards to form the call frame stack, while the chunks are
**	singly linked forward.  Since the chunk size is a known
**	constant, it's possible to quickly deduce the chunk a call
**	lives in from its pointer and the remaining size in the chunk.
**
***********************************************************************/

struct Reb_Chunk;

#define CS_CHUNK_PAYLOAD (2048 - sizeof(struct Reb_Chunk*))

struct Reb_Chunk {
	struct Reb_Chunk *next;
	REBYTE payload[CS_CHUNK_PAYLOAD];
};

struct Reb_Call {
	// How many bytes are left in the memory chunk this call frame lives in
	// (its own size has already been subtracted from the amount)
	REBINT chunk_left;

	struct Reb_Call *prior;

	// In an ideal world, it would not be possible for code to get its hands
	// on words that had been bound into a specific call frame while it
	// was still being formed...because no executing code would have access
	// to words that were linked into it.  Unfortunately with stack-relative
	// addressing, they can get that access:
	//
	//		leaker: func [/eval e /gimme g] [
	//			either gimme [return [g]] [reduce e]
	//		]
	//
	//		leaker/eval reduce leaker/gimme 10
	//
	// Since a leaked word from another instance of a function can give
	// access to a call frame during its formation, we need a way to tell
	// when a call frame is finished forming and a candidate for lookup
	// via Get_Var.  'args_ready' defaults to TRUE in Make_Call and then
	// is set to FALSE in Dispatch_Call when the function runs.
	//
	// !!! For optimization this boolean could be squeaked in lots of
	// other places, but a regular struct field for clarity right now.

	REBOOL args_ready;	// Function's arguments have finished evaluating

	REBCNT num_vars;	// !!! Redundant with VAL_FUNC_NUM_PARAMS()?

	REBVAL *out;		// where to write the function's output

	REBVAL func;			// copy (important!!) of function for call

	REBVAL where;			// block and index of execution
	REBVAL label;			// func word backtrace

	// these are "variables"...SELF, RETURN, args, locals
	REBVAL vars[1];		// (array exceeds struct, but cannot be [0] in C++)
};

#define DSF_NUM_VARS(c)	((c)->num_vars)

// Size must compensate -1 for the already-accounted-for length one array
#define DSF_SIZE(c) \
	( \
		sizeof(struct Reb_Call) \
		+ sizeof(REBVAL) * (DSF_NUM_VARS(c) > 0 ? DSF_NUM_VARS(c) - 1 : 0) \
	)

#define DSF_CHUNK(c) \
	cast(struct Reb_Chunk*, \
		cast(REBYTE*, (c)) \
		+ DSF_SIZE(c) \
		+ (c)->chunk_left \
		- sizeof(struct Reb_Chunk) \
	)


// !!! DSF is to be renamed (C)all (S)tack (P)ointer, but being left as DSF
// in the initial commit to try and cut back on the disruption seen in
// one commit, as there are already a lot of changes.

#define DSF (CS_Running + 0) // avoid assignment to DSF via + 0

#define SET_DSF(c) \
	( \
		CS_Running = (c), \
		(c) ? cast(void, (c)->args_ready = TRUE) : NOOP \
	)

#define DSF_OUT(c)		((c)->out)
#define PRIOR_DSF(c)	((c)->prior)
#define DSF_WHERE(c)	c_cast(const REBVAL*, &(c)->where)
#define DSF_LABEL(c)	c_cast(const REBVAL*, &(c)->label)
#define DSF_FUNC(c)		c_cast(const REBVAL*, &(c)->func)
#define DSF_RETURN(c)	coming@soon

// VARS includes (*will* include) RETURN dispatching value, locals...
#ifdef NDEBUG
	#define DSF_VAR(c,n)	(&(c)->vars[(n) - 1])
#else
	#define DSF_VAR(c,n)	DSF_VAR_Debug((c), (n)) // checks arg index bound
#endif

// ARGS is the parameters and refinements
#define DSF_ARG(c,n)	DSF_VAR((c), (n) - 1 + FIRST_PARAM_INDEX)
#define DSF_NUM_ARGS(c)	(DSF_NUM_VARS(c) - (FIRST_PARAM_INDEX - 1))

// !!! The function spec numbers words according to their position.  With
// definitional return, 0 is SELF, 1 is the RETURN, 2 is the first argument.
// (without, 1 is the first argument).  This layout is in flux as the
// workings of locals are rethought...their most sensible location would
// probably be between the RETURN and the arguments.

// Reference from ds that points to current return value:
#define D_OUT			DSF_OUT(call_)
#define D_ARG(n)		DSF_ARG(call_, (n))
#define D_REF(n)		(!IS_NONE(D_ARG(n)))

// Functions should generally not to detect the arity they were invoked with,
// (and it doesn't make sense as most implementations get the full list of
// arguments and refinements).  However, several dispatches may go through
// actions and other locations.  IS_BINARY_ACTION() and other functions could
// be used to do this more gracefully, but actions need review anyway
#define DS_ARGC			DSF_NUM_ARGS(call_)
