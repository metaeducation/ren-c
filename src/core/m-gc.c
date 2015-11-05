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
**  Module:  m-gc.c
**  Summary: main memory garbage collection
**  Section: memory
**  Author:  Carl Sassenrath, Ladislav Mecir, HostileFork
**  Notes:
**
**		The garbage collector is based on a conventional "mark and sweep":
**
**			https://en.wikipedia.org/wiki/Tracing_garbage_collection
**
**		From an optimization perspective, there is an attempt to not incur
**		function call overhead just to check if a GC-aware item has its
**		SER_MARK flag set.  So the flag is checked by a macro before making
**		any calls to process the references inside of an item.
**
**		"Shallow" marking only requires setting the flag, and is suitable for
**		series like strings (which are not containers for other REBVALs).  In
**		debug builds shallow marking is done with a function anyway, to give
**		a place to put assertion code or set breakpoints to catch when a
**		shallow mark is set (when that is needed).
**
**		"Deep" marking was originally done with recursion, and the recursion
**		would stop whenever a mark was hit.  But this meant deeply nested
**		structures could quickly wind up overflowing the C stack.  Consider:
**
**			a: copy []
**			loop 200'000 [a: append/only copy [] a]
**			recycle
**
**		The simple solution is that when an unmarked item is hit that it is
**		marked and put into a queue for processing (instead of recursed on the
**		spot.  This queue is then handled as soon as the marking stack is
**		exited, and the process repeated until no more items are queued.
**
**	  Regarding the two stages:
**
**		MARK -	Mark all series and gobs ("collectible values")
**				that can be found in:
**
**				Root Block: special structures and buffers
**				Task Block: special structures and buffers per task
**				Data Stack: current state of evaluation
**				Safe Series: saves the last N allocations
**
**		SWEEP - Free all collectible values that were not marked.
**
**	  GC protection methods:
**
**		KEEP flag - protects an individual series from GC, but
**			does not protect its contents (if it holds values).
**			Reserved for non-block system series.
**
**		Root_Context - protects all series listed. This list is
**			used by Sweep as the root of the in-use memory tree.
**			Reserved for important system series only.
**
**		Task_Context - protects all series listed. This list is
**			the same as Root, but per the current task context.
**
**		Save_Series - protects temporary series. Used with the
**			SAVE_SERIES and UNSAVE_SERIES macros. Throws and errors
**			must roll back this series to avoid "stuck" memory.
**
**		Safe_Series - protects last MAX_SAFE_SERIES series from GC.
**			Can only be used if no deeply allocating functions are
**			called within the scope of its protection. Not affected
**			by throws and errors.
**
**		Data_Stack - all values in the data stack that are below
**			the TOP (DSP) are automatically protected. This is a
**			common protection method used by native functions.
**
**		DONE flag - do not scan the series; it has no links.
**
***********************************************************************/

#include "sys-core.h"
#include "reb-evtypes.h"

//-- For Serious Debugging:
#ifdef WATCH_GC_VALUE
REBSER *Watcher = 0;
REBVAL *WatchVar = 0;
REBVAL *GC_Break_Point(REBVAL *val) {return val;}
REBVAL *N_watch(struct Reb_Frame *frame, REBVAL **inter_block)
{
	WatchVar = Get_Word(FRM_ARG1(frame));
	Watcher = VAL_SERIES(WatchVar);
	SET_INTEGER(FRM_ARG1(frame), 0);
	return Nothing;
}
#endif

// This can be put below
#ifdef WATCH_GC_VALUE
			if (Watcher && ser == Watcher)
				GC_Break_Point(val);

		// for (n = 0; n < depth * 2; n++) Prin_Str(" ");
		// Mark_Count++;
		// Print("Mark: %s %x", TYPE_NAME(val), val);
#endif

// was static, but exported for Ren/C
/* static void Queue_Mark_Value_Deep(const REBVAL *val);*/

static void Push_Array_Marked_Deep(REBSER *series);

#ifndef NDEBUG
static void Mark_Series_Only_Debug(REBSER *ser);
#endif

/***********************************************************************
**
*/	static void Push_Array_Marked_Deep(REBSER *series)
/*
**		Note: Call MARK_ARRAY_DEEP or QUEUE_MARK_ARRAY_DEEP instead!
**
**		Submits the block into the deferred stack to be processed later
**		with Propagate_All_GC_Marks().  We have already set this series
**		mark as it's now "spoken for".  (Though we haven't marked its
**		dependencies yet, we want to prevent it from being wastefully
**		submitted multiple times by another reference that would still
**		see it as "unmarked".)
**
**		The data structure used for this processing is a stack and not
**		a queue (for performance reasons).  But when you use 'queue'
**		as a verb it has more leeway than as the CS noun, and can just
**		mean "put into a list for later processing", hence macro names.
**
***********************************************************************/
{
	if (!SERIES_GET_FLAG(series, SER_MANAGED)) {
		Debug_Fmt("Link to non-MANAGED item reached by GC");
		Panic_Series(series);
	}

	assert(!SERIES_GET_FLAG(series, SER_EXTERNAL));
	assert(Is_Array_Series(series));

    // set by calling macro (helps catch direct calls of this function)
	assert(SERIES_GET_FLAG(series, SER_MARK));

	// Add series to the end of the mark stack series and update terminator
	if (SERIES_FULL(GC_Mark_Stack)) Extend_Series(GC_Mark_Stack, 8);
	cast(REBSER **, GC_Mark_Stack->data)[GC_Mark_Stack->tail++] = series;
	cast(REBSER **, GC_Mark_Stack->data)[GC_Mark_Stack->tail] = NULL;
}

static void Propagate_All_GC_Marks(void);

#ifndef NDEBUG
	static REBOOL in_mark = FALSE;
#endif

// NOTE: The following macros uses S parameter multiple times, hence if S has
// side effects this will run that side-effect multiply.

// Deferred form for marking series that prevents potentially overflowing the
// C execution stack.

#define QUEUE_MARK_ARRAY_DEEP(s) \
    do { \
        assert(Is_Array_Series(s)); \
        if (!SERIES_GET_FLAG((s), SER_MARK)) { \
            SERIES_SET_FLAG((s), SER_MARK); \
			Push_Array_Marked_Deep(s); \
        } \
    } while (0)


// Non-Queued form for marking blocks.  Used for marking a *root set item*,
// don't recurse from within Mark_Value/Mark_Gob/Mark_Array_Deep/etc.

#define MARK_ARRAY_DEEP(s) \
	do { \
		assert(!in_mark); \
		QUEUE_MARK_ARRAY_DEEP(s); \
		Propagate_All_GC_Marks(); \
	} while (0)


// Non-Deep form of mark, to be used on non-BLOCK! series or a block series
// for which deep marking is not necessary (such as an 'typed' words block)

#ifdef NDEBUG
	#define MARK_SERIES_ONLY(s) SERIES_SET_FLAG((s), SER_MARK)
#else
	#define MARK_SERIES_ONLY(s) Mark_Series_Only_Debug(s)
#endif


// Assertion for making sure that all the deferred marks have been propagated

#define ASSERT_NO_GC_MARKS_PENDING() \
	assert(SERIES_TAIL(GC_Mark_Stack) == 0)


#if !defined(NDEBUG)
/***********************************************************************
**
*/	static void Mark_Series_Only_Debug(REBSER *series)
/*
**		Hook point for marking and tracing a single series mark.
**
***********************************************************************/
{
	if (!SERIES_GET_FLAG(series, SER_MANAGED)) {
		Debug_Fmt("Link to non-MANAGED item reached by GC");
		Panic_Series(series);
	}

	SERIES_SET_FLAG(series, SER_MARK);
}
#endif


/***********************************************************************
**
*/	static void Queue_Mark_Gob_Deep(REBGOB *gob)
/*
**		'Queue' refers to the fact that after calling this routine,
**		one will have to call Propagate_All_GC_Marks() to have the
**		deep transitive closure be guaranteed fully marked.
**
**		Note: only referenced blocks are queued, the GOB structure
**		itself is processed via recursion.  Deeply nested GOBs could
**		in theory overflow the C stack.
**
***********************************************************************/
{
	REBGOB **pane;
	REBCNT i;

	if (IS_GOB_MARK(gob)) return;

	MARK_GOB(gob);

	if (GOB_PANE(gob)) {
		SERIES_SET_FLAG(GOB_PANE(gob), SER_MARK);
		pane = GOB_HEAD(gob);
		for (i = 0; i < GOB_TAIL(gob); i++, pane++)
			Queue_Mark_Gob_Deep(*pane);
	}

	if (GOB_PARENT(gob)) Queue_Mark_Gob_Deep(GOB_PARENT(gob));

	if (GOB_CONTENT(gob)) {
		if (GOB_TYPE(gob) >= GOBT_IMAGE && GOB_TYPE(gob) <= GOBT_STRING)
			SERIES_SET_FLAG(GOB_CONTENT(gob), SER_MARK);
		else if (GOB_TYPE(gob) >= GOBT_DRAW && GOB_TYPE(gob) <= GOBT_EFFECT)
			QUEUE_MARK_ARRAY_DEEP(GOB_CONTENT(gob));
	}

	if (GOB_DATA(gob) && GOB_DTYPE(gob) && GOB_DTYPE(gob) != GOBD_INTEGER)
		QUEUE_MARK_ARRAY_DEEP(GOB_DATA(gob));
}


/***********************************************************************
**
*/	static void Queue_Mark_Field_Deep(const REBSTU *stu, struct Struct_Field *field)
/*
**		'Queue' refers to the fact that after calling this routine,
**		one will have to call Propagate_All_GC_Marks() to have the
**		deep transitive closure be guaranteed fully marked.
**
**		Note: only referenced blocks are queued, fields that are structs
**		will be processed via recursion.  Deeply nested structs could
**		in theory overflow the C stack.
**
***********************************************************************/
{
	if (field->type == STRUCT_TYPE_STRUCT) {
		unsigned int len = 0;
		REBSER *field_fields = field->fields;

		MARK_SERIES_ONLY(field_fields);
		QUEUE_MARK_ARRAY_DEEP(field->spec);

		for (len = 0; len < field_fields->tail; len++) {
			Queue_Mark_Field_Deep(
				stu, cast(struct Struct_Field*, SERIES_SKIP(field_fields, len))
			);
		}
	}
	else if (field->type == STRUCT_TYPE_REBVAL) {
		REBCNT i;

		assert(field->size == sizeof(REBVAL));
		for (i = 0; i < field->dimension; i ++) {
			REBVAL *data = cast(REBVAL*,
				SERIES_SKIP(
					STRUCT_DATA_BIN(stu),
					STRUCT_OFFSET(stu) + field->offset + i * field->size
				)
			);

			if (field->done)
				Queue_Mark_Value_Deep(data);
		}
	}
	else {
		// ignore primitive datatypes
	}
}


/***********************************************************************
**
*/	static void Queue_Mark_Struct_Deep(const REBSTU *stu)
/*
**		'Queue' refers to the fact that after calling this routine,
**		one will have to call Propagate_All_GC_Marks() to have the
**		deep transitive closure be guaranteed fully marked.
**
**		Note: only referenced blocks are queued, the actual struct
**		itself is processed via recursion.  Deeply nested structs could
**		in theory overflow the C stack.
**
***********************************************************************/
{
	unsigned int len = 0;
	REBSER *series = NULL;

	// The spec is the only ANY-BLOCK! in the struct
	QUEUE_MARK_ARRAY_DEEP(stu->spec);

	MARK_SERIES_ONLY(stu->fields);
	MARK_SERIES_ONLY(STRUCT_DATA_BIN(stu));

	assert(!SERIES_GET_FLAG(stu->data, SER_EXTERNAL));
	assert(SERIES_TAIL(stu->data) == 1);
	MARK_SERIES_ONLY(stu->data);

	series = stu->fields;
	for (len = 0; len < series->tail; len++) {
		struct Struct_Field *field = cast(struct Struct_Field*,
			SERIES_SKIP(series, len)
		);

		Queue_Mark_Field_Deep(stu, field);
	}
}


/***********************************************************************
**
*/	static void Queue_Mark_Routine_Deep(const REBROT *rot)
/*
**		'Queue' refers to the fact that after calling this routine,
**		one will have to call Propagate_All_GC_Marks() to have the
**		deep transitive closure completely marked.
**
**		Note: only referenced blocks are queued, the routine's RValue
**		is processed via recursion.  Deeply nested RValue structs could
**		in theory overflow the C stack.
**
***********************************************************************/
{
	QUEUE_MARK_ARRAY_DEEP(ROUTINE_SPEC(rot));
	ROUTINE_SET_FLAG(ROUTINE_INFO(rot), ROUTINE_MARK);

	MARK_SERIES_ONLY(ROUTINE_FFI_ARG_TYPES(rot));
	QUEUE_MARK_ARRAY_DEEP(ROUTINE_FFI_ARG_STRUCTS(rot));
	MARK_SERIES_ONLY(ROUTINE_EXTRA_MEM(rot));

	if (IS_CALLBACK_ROUTINE(ROUTINE_INFO(rot))) {
		if (FUNC_BODY(&CALLBACK_FUNC(rot))) {
			QUEUE_MARK_ARRAY_DEEP(FUNC_BODY(&CALLBACK_FUNC(rot)));
			QUEUE_MARK_ARRAY_DEEP(FUNC_SPEC(&CALLBACK_FUNC(rot)));
			SERIES_SET_FLAG(FUNC_ARGS(&CALLBACK_FUNC(rot)), SER_MARK);
		}
		else {
			// may be null if called before the callback! is fully constructed
		}
	} else {
		if (ROUTINE_GET_FLAG(ROUTINE_INFO(rot), ROUTINE_VARARGS)) {
			if (ROUTINE_FIXED_ARGS(rot))
				QUEUE_MARK_ARRAY_DEEP(ROUTINE_FIXED_ARGS(rot));

			if (ROUTINE_ALL_ARGS(rot))
				QUEUE_MARK_ARRAY_DEEP(ROUTINE_ALL_ARGS(rot));
		}

		if (ROUTINE_LIB(rot))
			MARK_LIB(ROUTINE_LIB(rot));
		else {
			// may be null if called before the routine! is fully constructed
		}
	}
}


/***********************************************************************
**
*/	static void Queue_Mark_Event_Deep(const REBVAL *value)
/*
**		'Queue' refers to the fact that after calling this routine,
**		one will have to call Propagate_All_GC_Marks() to have the
**		deep transitive closure completely marked.
**
***********************************************************************/
{
	REBREQ *req;

	if (
		IS_EVENT_MODEL(value, EVM_PORT)
		|| IS_EVENT_MODEL(value, EVM_OBJECT)
		|| (
			VAL_EVENT_TYPE(value) == EVT_DROP_FILE
			&& GET_FLAG(VAL_EVENT_FLAGS(value), EVF_COPIED)
		)
	) {
		// Comment says void* ->ser field of the REBEVT is a "port or object"
		QUEUE_MARK_ARRAY_DEEP(
			cast(REBSER*, VAL_EVENT_SER(m_cast(REBVAL*, value)))
		);
	}

	if (IS_EVENT_MODEL(value, EVM_DEVICE)) {
		// In the case of being an EVM_DEVICE event type, the port! will
		// not be in VAL_EVENT_SER of the REBEVT structure.  It is held
		// indirectly by the REBREQ ->req field of the event, which
		// in turn possibly holds a singly linked list of other requests.
		req = VAL_EVENT_REQ(value);

		while (req) {
			// Comment says void* ->port is "link back to REBOL port object"
			if (req->port)
				QUEUE_MARK_ARRAY_DEEP(cast(REBSER*, req->port));
			req = req->next;
		}
	}
}


/***********************************************************************
**
*/ static void Mark_Devices_Deep(void)
/*
**		Mark all devices. Search for pending requests.
**
**		This should be called at the top level, and as it is not
**		'Queued' it guarantees that the marks have been propagated.
**
***********************************************************************/
{
	REBDEV **devices = Host_Lib->devices;

	int d;
	for (d = 0; d < RDI_MAX; d++) {
		REBREQ *req;
		REBDEV *dev = devices[d];
		if (!dev)
			continue;

		for (req = dev->pending; req; req = req->next)
			if (req->port)
				MARK_ARRAY_DEEP(cast(REBSER*, req->port));
	}
}


/***********************************************************************
**
*/ static void Mark_Call_Frames_Deep(void)
/*
**		Mark all function call frames.  In addition to containing the
**		arguments that are referred to by pointer during a function
**		invocation (acquired via D_ARG(N) calls), it is able to point
**		to an arbitrary stable memory location for D_OUT.  This may
**		be giving awareness to the GC of a variable on the C stack
**		(for example).  This also keeps the function value itself
**		live, as well as the "label" word and "where" block value.
**
**		Note that prior to a function invocation, the output value
**		slot is written with "safe" TRASH.  This helps the evaluator
**		catch cases of when a function dispatch doesn't consciously
**		write any value into the output in debug builds.  The GC is
**		willing to overlook this safe trash, however, and it will just
**		be an UNSET! in the release build.
**
**		This should be called at the top level, and not from inside a
**		Propagate_All_GC_Marks().  All marks will be propagated.
**
***********************************************************************/
{
	struct Reb_Call *call = CS_Top;

	while (call) {
		REBCNT index;

		Queue_Mark_Value_Deep(DSF_OUT(call));
		Queue_Mark_Value_Deep(DSF_FUNC(call));
		Queue_Mark_Value_Deep(DSF_WHERE(call));
		Queue_Mark_Value_Deep(DSF_LABEL(call));

		for (index = 1; index <= call->num_vars; index++)
			Queue_Mark_Value_Deep(DSF_VAR(call, index));

		Propagate_All_GC_Marks();

		call = PRIOR_DSF(call);
	}
}


/***********************************************************************
**
*/	void Queue_Mark_Value_Deep(const REBVAL *val)
/*
**		This routine is not marked `static` because it is needed by
**		Ren/C++ in order to implement its GC_Mark_Hook.
**
***********************************************************************/
{
	REBSER *ser = NULL;

	// If this happens, it means somehow Recycle() got called between
	// when an `if (Do_XXX_Throws())` branch was taken and when the throw
	// should have been caught up the stack (before any more calls made).
	//
	assert(!THROWN(val));

	switch (VAL_TYPE(val)) {
		case REB_UNSET:
			break;

		case REB_TYPESET:
			// As long as typeset is encoded as 64 bits, there's no issue
			// of having to keep alive "user types" or other things...but
			// that might be needed in the future.
			//
			// The symbol stored for object/frame typesets is effectively
			// unbound, and hence has no frame to be preserved.
			break;

		case REB_HANDLE:
			break;

		case REB_DATATYPE:
			// Type spec is allowed to be NULL.  See %typespec.r file
			if (VAL_TYPE_SPEC(val))
				QUEUE_MARK_ARRAY_DEEP(VAL_TYPE_SPEC(val));
			break;

		case REB_ERROR:
			QUEUE_MARK_ARRAY_DEEP(VAL_ERR_OBJECT(val));
			break;

		case REB_TASK: // not yet implemented
			break;

		case REB_FRAME:
			// Mark special word list.
			//
			// At the moment this list contains no values which would
			// require being seen by the GC...however skipping over the
			// values is a limited optimization.  (For instance: symbols
			// may become GC'd and need to see the symbol references inside
			// the values, or typesets might contain dynamically allocated
			// arrays of user types).
			//
			// !!! A more global optimization would be if there was a flag
			// that was maintained about whether there might be any GC'able
			// values in an array.  It could start out saying there may
			// be...but then if it did a visit and didn't see any mark it
			// as not needing GC.  Then modifications dirty that bit.
			//
			QUEUE_MARK_ARRAY_DEEP(VAL_FRM_KEYLIST(val));

			if (VAL_FRM_SPEC(val))
				QUEUE_MARK_ARRAY_DEEP(VAL_FRM_SPEC(val));
			// !!! See code below for ANY-WORD! which also deals with FRAME!
			break;

		case REB_PORT:
		case REB_OBJECT:
			// Objects currently only have a FRAME, but that protects the
			// keys wordlist via the FRAME! value in the first slot (which
			// will be visited along with mapped values via this deep mark)
			QUEUE_MARK_ARRAY_DEEP(VAL_OBJ_FRAME(val));
			break;

		case REB_MODULE:
			// A module is an object with an optional body (they currently
			// do not use the body)
			QUEUE_MARK_ARRAY_DEEP(VAL_OBJ_FRAME(val));
			if (VAL_MOD_BODY(val))
				QUEUE_MARK_ARRAY_DEEP(VAL_MOD_BODY(val));
			break;

		case REB_FUNCTION:
		case REB_COMMAND:
		case REB_CLOSURE:
			QUEUE_MARK_ARRAY_DEEP(VAL_FUNC_BODY(val));
		case REB_NATIVE:
		case REB_ACTION:
			QUEUE_MARK_ARRAY_DEEP(VAL_FUNC_SPEC(val));
			QUEUE_MARK_ARRAY_DEEP(VAL_FUNC_PARAMLIST(val));
			break;

		case REB_WORD:	// (and also used for function STACK backtrace frame)
		case REB_SET_WORD:
		case REB_GET_WORD:
		case REB_LIT_WORD:
		case REB_REFINEMENT:
		case REB_ISSUE:
			ser = VAL_WORD_FRAME(val);
			if (ser) {
				// Word is bound, so mark its context (which may be a FRAME!
				// series or an identifying function word series).  All
				// bound words should keep their contexts from being GC'd...
				// even stack-relative contexts for functions.

				QUEUE_MARK_ARRAY_DEEP(ser);
			}
			else {
			#ifndef NDEBUG
				// Word is not bound to any frame; which means its index
				// is uninitialized in release builds.  But in debug builds
				// we require it to be a special value for checking.

				if (VAL_WORD_INDEX(val) != WORD_INDEX_UNBOUND)
					Panic_Series(ser);
			#endif
			}
			break;

		case REB_NONE:
		case REB_LOGIC:
		case REB_INTEGER:
		case REB_DECIMAL:
		case REB_PERCENT:
		case REB_MONEY:
		case REB_TIME:
		case REB_DATE:
		case REB_CHAR:
		case REB_PAIR:
		case REB_TUPLE:
			break;

		case REB_STRING:
		case REB_BINARY:
		case REB_FILE:
		case REB_EMAIL:
		case REB_URL:
		case REB_TAG:
		case REB_BITSET:
			ser = VAL_SERIES(val);
			assert(SERIES_WIDE(ser) <= sizeof(REBUNI));
			MARK_SERIES_ONLY(ser);
			break;

		case REB_IMAGE:
			//SERIES_SET_FLAG(VAL_SERIES_SIDE(val), SER_MARK); //????
			MARK_SERIES_ONLY(VAL_SERIES(val));
			break;

		case REB_VECTOR:
			MARK_SERIES_ONLY(VAL_SERIES(val));
			break;

		case REB_BLOCK:
		case REB_PAREN:
		case REB_PATH:
		case REB_SET_PATH:
		case REB_GET_PATH:
		case REB_LIT_PATH:
			ser = VAL_SERIES(val);
			assert(Is_Array_Series(ser) && SERIES_WIDE(ser) == sizeof(REBVAL));
			ASSERT_SERIES_TERM(ser);

			QUEUE_MARK_ARRAY_DEEP(ser);
			break;

		case REB_MAP:
			ser = VAL_SERIES(val);
			QUEUE_MARK_ARRAY_DEEP(ser);
			if (ser->extra.series)
				MARK_SERIES_ONLY(ser->extra.series);
			break;

		case REB_CALLBACK:
		case REB_ROUTINE:
			QUEUE_MARK_ARRAY_DEEP(VAL_ROUTINE_SPEC(val));
			QUEUE_MARK_ARRAY_DEEP(VAL_ROUTINE_ARGS(val));
			Queue_Mark_Routine_Deep(&VAL_ROUTINE(val));
			break;

		case REB_LIBRARY:
			MARK_LIB(VAL_LIB_HANDLE(val));
			QUEUE_MARK_ARRAY_DEEP(VAL_LIB_SPEC(val));
			break;

		case REB_STRUCT:
			Queue_Mark_Struct_Deep(&VAL_STRUCT(val));
			break;

		case REB_GOB:
			Queue_Mark_Gob_Deep(VAL_GOB(val));
			break;

		case REB_EVENT:
			Queue_Mark_Event_Deep(val);
			break;

		default: {
		#if !defined(NDEBUG)
			// We allow *safe* trash values to be on the stack at the time
			// of a garbage collection.  These will be UNSET! in the debug
			// builds and they would not interfere with GC (they only exist
			// so that at the end of a process you can confirm that if an
			// UNSET! is in the slot, it was written there purposefully)
			if (IS_TRASH(val)) {
				assert(VAL_TRASH_SAFE(val));
				break;
			}
		#endif

			panic Error_Invalid_Datatype(VAL_TYPE(val));
		}
	}
}


/***********************************************************************
**
*/	static void Mark_Array_Deep_Core(REBSER *array)
/*
**		Mark all series reachable from the array.
**
***********************************************************************/
{
	REBCNT len;

	assert(!SERIES_FREED(array)); // should not be reaching freed series

#if !defined(NDEBUG)
	// We should have marked this series to keep it from being doubly
	// added already...

	if (!SERIES_GET_FLAG(array, SER_MARK)) Panic_Series(array);

	// SER_FRAME is going to replace the FRAME! type for indicating frames,
	// with the full REBVAL of the frame-holder OBJECT! or otherwise in the
	// zero slot.  Keylist will be in the series extra field.  Start testing
	// for that condition now.
	if (SERIES_GET_FLAG(array, SER_FRAME))
		assert(IS_FRAME(BLK_HEAD(array)));
#endif

	assert(SERIES_TAIL(array) < SERIES_REST(array)); // overflow

	for (len = 0; len < array->tail; len++) {
		REBVAL *val = BLK_SKIP(array, len);
		// We should never reach the end before len above.
		// Exception is the stack itself.
		assert(VAL_TYPE(val) != REB_END || (array == DS_Series));
		if (VAL_TYPE(val) == REB_FRAME) {
			assert(len == 0);
			ASSERT_FRAME(array);
			if ((array == VAL_SERIES(ROOT_ROOT)) || (array == Task_Series)) {
				// !!! Currently it is allowed that the root frames not
				// have a wordlist.  This distinct behavior accomodation is
				// not worth having the variance of behavior, but since
				// it's there for now... allow it for just those two.

				if(!VAL_FRM_KEYLIST(val))
					continue;
			}
		}
		Queue_Mark_Value_Deep(val);
	}

#if !defined(NDEBUG)
	if (!IS_END(BLK_SKIP(array, len))) {
		Debug_Fmt("Found badly terminated array in Mark_Array_Deep_Core()");
		Panic_Series(array);
	}
#endif
}


/***********************************************************************
**
*/	ATTRIBUTE_NO_SANITIZE_ADDRESS static REBCNT Sweep_Series(REBOOL shutdown)
/*
**		Scans all series in all segments that are part of the
**		SERIES_POOL.  If a series had its lifetime management
**		delegated to the garbage collector with MANAGE_SERIES(),
**		then if it didn't get "marked" as live during the marking
**		phase then free it.
**
**		The current exception is that any GC-managed series that has
**		been marked with the SER_KEEP flag will not be freed--unless
**		this sweep call is during shutdown.  During shutdown, those
**		kept series will be freed as well.
**
**		!!! Review the idea of SER_KEEP, as it is a lot like
**		Guard_Series (which was deleted).  Although SER_KEEP offers a
**		less inefficient way to flag a series as protected from the
**		garbage collector, it can be put on and left for an arbitrary
**		amount of time...making it seem contentious with the idea of
**		delegating it to the garbage collector in the first place.
**
***********************************************************************/
{
	REBSEG *seg;
	REBCNT count = 0;

	for (seg = Mem_Pools[SERIES_POOL].segs; seg; seg = seg->next) {
		REBSER *series = cast(REBSER *, seg + 1);
		REBCNT n;
		for (n = Mem_Pools[SERIES_POOL].units; n > 0; n--, series++) {
			// See notes on Make_Node() about how the first allocation of a
			// unit zero-fills *most* of it.  But after that it's up to the
			// caller of Free_Node() to zero out whatever bits it uses to
			// indicate "freeness".  We check the zeroness of the `wide`.
			if (SERIES_FREED(series))
				continue;

			if (SERIES_GET_FLAG(series, SER_MANAGED)) {
				if (shutdown || !SERIES_GET_FLAG(series, SER_MARK)) {
					GC_Kill_Series(series);
					count++;
				} else
					SERIES_CLR_FLAG(series, SER_MARK);
			}
			else
				assert(!SERIES_GET_FLAG(series, SER_MARK));
		}
	}

	return count;
}


/***********************************************************************
**
*/	ATTRIBUTE_NO_SANITIZE_ADDRESS static REBCNT Sweep_Gobs(void)
/*
**		Free all unmarked gobs.
**
**		Scans all gobs in all segments that are part of the
**		GOB_POOL. Free gobs that have not been marked.
**
***********************************************************************/
{
	REBSEG	*seg;
	REBGOB	*gob;
	REBCNT  n;
	REBCNT	count = 0;

	for (seg = Mem_Pools[GOB_POOL].segs; seg; seg = seg->next) {
		gob = (REBGOB *) (seg + 1);
		for (n = Mem_Pools[GOB_POOL].units; n > 0; n--) {
			if (IS_GOB_USED(gob)) {
				if (IS_GOB_MARK(gob))
					UNMARK_GOB(gob);
				else {
					Free_Gob(gob);
					count++;
				}
			}
			gob++;
		}
	}

	return count;
}


/***********************************************************************
**
*/	ATTRIBUTE_NO_SANITIZE_ADDRESS static REBCNT Sweep_Libs(void)
/*
**		Free all unmarked libs.
**
**		Scans all libs in all segments that are part of the
**		LIB_POOL. Free libs that have not been marked.
**
***********************************************************************/
{
	REBSEG	*seg;
	REBLHL	*lib;
	REBCNT  n;
	REBCNT	count = 0;

	for (seg = Mem_Pools[LIB_POOL].segs; seg; seg = seg->next) {
		lib = (REBLHL *) (seg + 1);
		for (n = Mem_Pools[LIB_POOL].units; n > 0; n--) {
			if (IS_USED_LIB(lib)) {
				if (IS_MARK_LIB(lib))
					UNMARK_LIB(lib);
				else {
					UNUSE_LIB(lib);
					Free_Node(LIB_POOL, (REBNOD*)lib);
					count++;
				}
			}
			lib++;
		}
	}

	return count;
}


/***********************************************************************
**
*/	ATTRIBUTE_NO_SANITIZE_ADDRESS static REBCNT Sweep_Routines(void)
/*
**		Free all unmarked routines.
**
**		Scans all routines in all segments that are part of the
**		RIN_POOL. Free routines that have not been marked.
**
***********************************************************************/
{
	REBSEG	*seg;
	REBRIN	*info;
	REBCNT  n;
	REBCNT	count = 0;

	for (seg = Mem_Pools[RIN_POOL].segs; seg; seg = seg->next) {
		info = (REBRIN *) (seg + 1);
		for (n = Mem_Pools[RIN_POOL].units; n > 0; n--) {
			if (ROUTINE_GET_FLAG(info, ROUTINE_USED)) {
				if (ROUTINE_GET_FLAG(info, ROUTINE_MARK))
					ROUTINE_CLR_FLAG(info, ROUTINE_MARK);
				else {
					ROUTINE_CLR_FLAG(info, ROUTINE_USED);
					Free_Routine(info);
					count ++;
				}
			}
			info ++;
		}
	}

	return count;
}


/***********************************************************************
**
*/	static void Propagate_All_GC_Marks(void)
/*
**		The Mark Stack is a series containing series pointers.  They
**		have already had their SER_MARK set to prevent being added
**		to the stack multiple times, but the items they can reach
**		are not necessarily marked yet.
**
**		Processing continues until all reachable items from the mark
**		stack are known to be marked.
**
***********************************************************************/
{
	assert(!in_mark);
	while (GC_Mark_Stack->tail != 0) {
		// Data pointer may change in response to an expansion during
		// Mark_Array_Deep_Core(), so must be refreshed on each loop.
		REBSER *array =
			cast(REBSER **, GC_Mark_Stack->data)[--GC_Mark_Stack->tail];

		// Drop the series we are processing off the tail, as we could be
		// queuing more of them (hence increasing the tail).
		cast(REBSER **, GC_Mark_Stack->data)[GC_Mark_Stack->tail] = NULL;

		Mark_Array_Deep_Core(array);
	}
}


/***********************************************************************
**
*/	REBCNT Recycle_Core(REBOOL shutdown)
/*
**		Recycle memory no longer needed.
**
***********************************************************************/
{
	REBINT n;
	REBCNT count;

	//Debug_Num("GC", GC_Disabled);

	ASSERT_NO_GC_MARKS_PENDING();

	// If disabled, exit now but set the pending flag.
	if (GC_Disabled || !GC_Active) {
		SET_SIGNAL(SIG_RECYCLE);
		//Print("pending");
		return 0;
	}

	if (Reb_Opts->watch_recycle) Debug_Str(cs_cast(BOOT_STR(RS_WATCH, 0)));

	GC_Disabled = 1;

	PG_Reb_Stats->Recycle_Counter++;
	PG_Reb_Stats->Recycle_Series = Mem_Pools[SERIES_POOL].free;

	PG_Reb_Stats->Mark_Count = 0;

	// WARNING: These terminate existing open blocks. This could
	// be a problem if code is building a new value at the tail,
	// but has not yet updated the TAIL marker.
	VAL_TERM_ARRAY(TASK_BUF_EMIT);
	VAL_TERM_ARRAY(TASK_BUF_COLLECT);

	// MARKING PHASE: the "root set" from which we determine the liveness
	// (or deadness) of a series.  If we are shutting down, we are freeing
	// *all* of the series that are managed by the garbage collector, so
	// we don't mark anything as live.

	if (!shutdown) {
		REBSER **sp;
		REBVAL **vp;

		// Mark series stack (temp-saved series):
		sp = cast(REBSER**, GC_Series_Guard->data);
		for (n = SERIES_TAIL(GC_Series_Guard); n > 0; n--, sp++) {
			if (Is_Array_Series(*sp))
				MARK_ARRAY_DEEP(*sp);
			else
				MARK_SERIES_ONLY(*sp);
		}

		// Mark value stack (temp-saved values):
		vp = cast(REBVAL**, GC_Value_Guard->data);
		for (n = SERIES_TAIL(GC_Value_Guard); n > 0; n--, vp++) {
			Queue_Mark_Value_Deep(*vp);
			Propagate_All_GC_Marks();
		}

		// Mark all root series:
		MARK_ARRAY_DEEP(VAL_SERIES(ROOT_ROOT));
		MARK_ARRAY_DEEP(Task_Series);

		// Mark potential error object from callback!
		Queue_Mark_Value_Deep(&Callback_Error);
		Propagate_All_GC_Marks();

		// !!! This hook point is an interim measure for letting a host
		// mark REBVALs that it is holding onto which are not contained in
		// series.  It is motivated by Ren/C++, which wraps REBVALs in
		// `ren::Value` class instances, and is able to enumerate the
		// "live" classes (they "die" when the destructor runs).
		//
		if (GC_Mark_Hook) {
			(*GC_Mark_Hook)();
			Propagate_All_GC_Marks();
		}

		// Mark all devices:
		Mark_Devices_Deep();

		// Mark function call frames:
		Mark_Call_Frames_Deep();
	}

	// SWEEPING PHASE

	// this needs to run before Sweep_Series(), because Routine has series
	// with pointers, which can't be simply discarded by Sweep_Series
	count = Sweep_Routines();

	count += Sweep_Series(shutdown);
	count += Sweep_Gobs();
	count += Sweep_Libs();

	CHECK_MEMORY(4);

	// Compute new stats:
	PG_Reb_Stats->Recycle_Series = Mem_Pools[SERIES_POOL].free - PG_Reb_Stats->Recycle_Series;
	PG_Reb_Stats->Recycle_Series_Total += PG_Reb_Stats->Recycle_Series;
	PG_Reb_Stats->Recycle_Prior_Eval = Eval_Cycles;

	if (GC_Ballast <= VAL_INT32(TASK_BALLAST) / 2
		&& VAL_INT64(TASK_BALLAST) < MAX_I32) {
		//increasing ballast by half
		VAL_INT64(TASK_BALLAST) /= 2;
		VAL_INT64(TASK_BALLAST) *= 3;
	} else if (GC_Ballast >= VAL_INT64(TASK_BALLAST) * 2) {
		//reduce ballast by half
		VAL_INT64(TASK_BALLAST) /= 2;
	}

	/* avoid overflow */
	if (VAL_INT64(TASK_BALLAST) < 0 || VAL_INT64(TASK_BALLAST) >= MAX_I32) {
		VAL_INT64(TASK_BALLAST) = MAX_I32;
	}

	GC_Ballast = VAL_INT32(TASK_BALLAST);
	GC_Disabled = 0;

	if (Reb_Opts->watch_recycle) Debug_Fmt(cs_cast(BOOT_STR(RS_WATCH, 1)), count);

	ASSERT_NO_GC_MARKS_PENDING();

	return count;
}


/***********************************************************************
**
*/	REBCNT Recycle(void)
/*
**		Recycle memory no longer needed.
**
***********************************************************************/
{
	// Default to not passing the `shutdown` flag.
	return Recycle_Core(FALSE);
}


/***********************************************************************
**
*/	void Guard_Series_Core(REBSER *series)
/*
***********************************************************************/
{
	// It would seem there isn't any reason to save a series from being
	// garbage collected if it is already invisible to the garbage
	// collector.  But some kind of "saving" feature which added a
	// non-managed series in as if it were part of the root set would
	// be useful.  That would be for cases where you are building a
	// series up from constituent values but might want to abort and
	// manually free it.  For the moment, we don't have that feature.
	ASSERT_SERIES_MANAGED(series);

	if (SERIES_FULL(GC_Series_Guard)) Extend_Series(GC_Series_Guard, 8);

	cast(REBSER **, GC_Series_Guard->data)[GC_Series_Guard->tail] = series;
	GC_Series_Guard->tail++;
}


/***********************************************************************
**
*/	void Guard_Value_Core(const REBVAL *value)
/*
***********************************************************************/
{
	// Cheap check; we don't want you to save any values that wouldn't
	// be safe if the GC saw them.  We exclude REB_END to catch zero
	// initializations.
	assert(VAL_TYPE(value) > REB_END && VAL_TYPE(value) < REB_MAX);

#ifdef STRESS_CHECK_GUARD_VALUE_POINTER
	// Technically we should never call this routine to guard a value that
	// lives inside of a series.  Not only would we have to guard the
	// containing series, we would also have to lock the series from
	// being able to resize and reallocate the data pointer.  But this is
	// a somewhat expensive check, so it's only feasible to run occasionally.
	ASSERT_NOT_IN_SERIES_DATA(value);
#endif

	if (SERIES_FULL(GC_Value_Guard)) Extend_Series(GC_Value_Guard, 8);

	cast(const REBVAL **, GC_Value_Guard->data)[GC_Value_Guard->tail] = value;
	GC_Value_Guard->tail++;
}


/***********************************************************************
**
*/	void Init_GC(void)
/*
**		Initialize garbage collector.
**
***********************************************************************/
{
	GC_Active = 0;			// TRUE when recycle is enabled (set by RECYCLE func)
	GC_Disabled = 0;		// GC disabled counter for critical sections.
	GC_Ballast = MEM_BALLAST;

	// Temporary series protected from GC. Holds series pointers.
	GC_Series_Guard = Make_Series(15, sizeof(REBSER *), MKS_NONE);
	LABEL_SERIES(GC_Series_Guard, "gc series save");

	// Temporary values protected from GC. Holds value pointers.
	GC_Value_Guard = Make_Series(15, sizeof(REBVAL *), MKS_NONE);
	LABEL_SERIES(GC_Value_Guard, "gc value save");

	// The marking queue used in lieu of recursion to ensure that deeply
	// nested structures don't cause the C stack to overflow.
	GC_Mark_Stack = Make_Series(100, sizeof(REBSER *), MKS_NONE);
	TERM_SEQUENCE(GC_Mark_Stack);
	LABEL_SERIES(GC_Mark_Stack, "gc mark stack");
}


/***********************************************************************
**
*/	void Shutdown_GC(void)
/*
***********************************************************************/
{
	Free_Series(GC_Series_Guard);
	Free_Series(GC_Value_Guard);
	Free_Series(GC_Mark_Stack);
}
