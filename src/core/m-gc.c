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
**		DISABLE_GC - macro that turns off GC. A quick way to avoid
**			GC, but must only be used for well-behaved sections
**			or could cause substantial memory growth.
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

static void Queue_Mark_Value_Deep(const REBVAL *val);

static void Push_Block_Marked_Deep(REBSER *series);

static void Mark_Series_Only_Debug(REBSER *ser);

/***********************************************************************
**
*/	static void Push_Block_Marked_Deep(REBSER *series)
/*
**		Note: Call MARK_BLOCK_DEEP or QUEUE_MARK_BLOCK_DEEP instead!
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
	assert(!SERIES_GET_FLAG(series, SER_EXTERNAL));
	assert(IS_BLOCK_SERIES(series));

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

#define QUEUE_MARK_BLOCK_DEEP(s) \
    do { \
        assert(IS_BLOCK_SERIES(s)); \
        if (!SERIES_GET_FLAG((s), SER_MARK)) { \
            SERIES_SET_FLAG((s), SER_MARK); \
            Push_Block_Marked_Deep(s); \
        } \
    } while (0)


// Non-Queued form for marking blocks.  Used for marking a *root set item*,
// don't recurse from within Mark_Value/Mark_Gob/Mark_Block_Deep/etc.

#define MARK_BLOCK_DEEP(s) \
	do { \
		assert(!in_mark); \
		QUEUE_MARK_BLOCK_DEEP(s); \
		Propagate_All_GC_Marks(); \
	} while (0)


// Non-Deep form of mark, to be used on non-BLOCK! series or a block series
// for which deep marking is not necessary (such as an 'typed' words block)

#ifdef NDEBUG
	#define MARK_SERIES_ONLY(s) SERIES_SET_FLAG((s), SER_MARK)
#else
	#define MARK_SERIES_ONLY(s) Mark_Series_Only_Debug(s)
#endif


// Typed word blocks contain REBWRS-style words, which have type information
// instead of a binding.  They shouldn't have any other types in them so we
// don't need to mark deep...BUT doesn't hurt to check in debug builds!

#define MARK_TYPED_WORDS_BLOCK(s) \
	do { \
		ASSERT_TYPED_WORDS_BLOCK(s); \
		MARK_SERIES_ONLY(s); \
	} while (0)


// Assertion for making sure that all the deferred marks have been propagated

#define ASSERT_NO_GC_MARKS_PENDING() \
	assert(SERIES_TAIL(GC_Mark_Stack) == 0)


#if !defined(NDEBUG)
/***********************************************************************
**
*/	static void Mark_Series_Only_Debug(REBSER *ser)
/*
**		Hook point for marking and tracing a single series mark.
**
***********************************************************************/
{
	SERIES_SET_FLAG(ser, SER_MARK);
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
			QUEUE_MARK_BLOCK_DEEP(GOB_CONTENT(gob));
	}

	if (GOB_DATA(gob) && GOB_DTYPE(gob) && GOB_DTYPE(gob) != GOBD_INTEGER)
		QUEUE_MARK_BLOCK_DEEP(GOB_DATA(gob));
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
		QUEUE_MARK_BLOCK_DEEP(field->spec);

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
	QUEUE_MARK_BLOCK_DEEP(stu->spec);

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
	QUEUE_MARK_BLOCK_DEEP(ROUTINE_SPEC(rot));
	ROUTINE_SET_FLAG(ROUTINE_INFO(rot), ROUTINE_MARK);

	MARK_SERIES_ONLY(ROUTINE_FFI_ARG_TYPES(rot));
	QUEUE_MARK_BLOCK_DEEP(ROUTINE_FFI_ARG_STRUCTS(rot));
	MARK_SERIES_ONLY(ROUTINE_EXTRA_MEM(rot));

	if (IS_CALLBACK_ROUTINE(ROUTINE_INFO(rot))) {
		if (FUNC_BODY(&CALLBACK_FUNC(rot))) {
			QUEUE_MARK_BLOCK_DEEP(FUNC_BODY(&CALLBACK_FUNC(rot)));
			QUEUE_MARK_BLOCK_DEEP(FUNC_SPEC(&CALLBACK_FUNC(rot)));
			SERIES_SET_FLAG(FUNC_ARGS(&CALLBACK_FUNC(rot)), SER_MARK);
		}
		else {
			// may be null if called before the callback! is fully constructed
		}
	} else {
		if (ROUTINE_GET_FLAG(ROUTINE_INFO(rot), ROUTINE_VARARGS)) {
			if (ROUTINE_FIXED_ARGS(rot))
				MARK_TYPED_WORDS_BLOCK(ROUTINE_FIXED_ARGS(rot));

			if (ROUTINE_ALL_ARGS(rot))
				MARK_TYPED_WORDS_BLOCK(ROUTINE_ALL_ARGS(rot));
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
		QUEUE_MARK_BLOCK_DEEP(
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
				QUEUE_MARK_BLOCK_DEEP(cast(REBSER*, req->port));
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
				MARK_BLOCK_DEEP(cast(REBSER*, req->port));
	}
}


/***********************************************************************
**
*/ static void Mark_Call_Frames_Deep(void)
/*
**		Mark all function call frames.  At the moment, this is mostly
**		taken care of by the marking of the data stack itself...since
**		the call frames put their values on the data stack.  The one
**		exception is the return value, which only *indirectly*
**		implicates a value (which may or may not live on the data
**		stack) by storing a pointer into a handle.  We must extract
**		that REBVAL* in order for the garbage collector to see it,
**		as the handle would be opaque to it otherwise.
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
*/	static void Queue_Mark_Value_Deep(const REBVAL *val)
/*
***********************************************************************/
{
	REBSER *ser = NULL;

	if (THROWN(val)) {
		// Running GC where it can get a chance to see a THROWN value
		// is invalid because it is related to a temporary value saved
		// with THROWN_ARG.  So if the GC sees the thrown, it is not
		// seeing the THROWN_ARG.

		// !!! It's not clear if this should crash or not.
		// Aggressive Recycle() forces this to happen, review.

		// Panic(RP_THROW_IN_GC);
	}

	switch (VAL_TYPE(val)) {
		case REB_UNSET:
		case REB_TYPESET:
		case REB_HANDLE:
			break;

		case REB_DATATYPE:
			// Type spec is allowed to be NULL.  See %typespec.r file
			if (VAL_TYPE_SPEC(val))
				QUEUE_MARK_BLOCK_DEEP(VAL_TYPE_SPEC(val));
			break;

		case REB_ERROR:
			QUEUE_MARK_BLOCK_DEEP(VAL_ERR_OBJECT(val));
			break;

		case REB_TASK: // not yet implemented
			break;

		case REB_FRAME:
			// Mark special word list. Contains no pointers because
			// these are special word bindings (to typesets if used).
			MARK_TYPED_WORDS_BLOCK(VAL_FRM_WORDS(val));
			if (VAL_FRM_SPEC(val))
				QUEUE_MARK_BLOCK_DEEP(VAL_FRM_SPEC(val));
			// !!! See code below for ANY-WORD! which also deals with FRAME!
			break;

		case REB_PORT:
		case REB_OBJECT:
			// Objects currently only have a FRAME, but that protects the
			// keys wordlist via the FRAME! value in the first slot (which
			// will be visited along with mapped values via this deep mark)
			QUEUE_MARK_BLOCK_DEEP(VAL_OBJ_FRAME(val));
			break;

		case REB_MODULE:
			// A module is an object with an optional body (they currently
			// do not use the body)
			QUEUE_MARK_BLOCK_DEEP(VAL_OBJ_FRAME(val));
			if (VAL_MOD_BODY(val))
				QUEUE_MARK_BLOCK_DEEP(VAL_MOD_BODY(val));
			break;

		case REB_FUNCTION:
		case REB_COMMAND:
		case REB_CLOSURE:
		case REB_REBCODE:
			QUEUE_MARK_BLOCK_DEEP(VAL_FUNC_BODY(val));
		case REB_NATIVE:
		case REB_ACTION:
			QUEUE_MARK_BLOCK_DEEP(VAL_FUNC_SPEC(val));
			MARK_TYPED_WORDS_BLOCK(VAL_FUNC_WORDS(val));
			break;

		case REB_WORD:	// (and also used for function STACK backtrace frame)
		case REB_SET_WORD:
		case REB_GET_WORD:
		case REB_LIT_WORD:
		case REB_REFINEMENT:
		case REB_ISSUE:
			// Special word used in word frame, stack, or errors:
			if (VAL_GET_EXT(val, EXT_WORD_TYPED)) break;

			ser = VAL_WORD_FRAME(val);
			if (ser) {
				// Word is bound, so mark its context (which may be a FRAME!
				// series or an identifying function word series).  All
				// bound words should keep their contexts from being GC'd...
				// even stack-relative contexts for functions.

				REBVAL *first;
				assert(SERIES_TAIL(ser) > 0);
				first = BLK_HEAD(ser);

				if (IS_FRAME(first)) {
					// It's referring to an OBJECT!-style FRAME, where the
					// first element is a FRAME! containing the word keys
					// and the rest of the elements are the data values
					QUEUE_MARK_BLOCK_DEEP(ser);
				}
				else {
					// It's referring to a FUNCTION!'s identifying series,
					// which should just be a list of 'typed' words.
					MARK_TYPED_WORDS_BLOCK(ser);
				}
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
			assert(IS_BLOCK_SERIES(ser) && SERIES_WIDE(ser) == sizeof(REBVAL));
			assert(IS_END(BLK_SKIP(ser, SERIES_TAIL(ser))) || ser == DS_Series);

			QUEUE_MARK_BLOCK_DEEP(ser);
			break;

		case REB_MAP:
			ser = VAL_SERIES(val);
			QUEUE_MARK_BLOCK_DEEP(ser);
			if (ser->extra.series)
				MARK_SERIES_ONLY(ser->extra.series);
			break;

		case REB_CALLBACK:
		case REB_ROUTINE:
			QUEUE_MARK_BLOCK_DEEP(VAL_ROUTINE_SPEC(val));
			QUEUE_MARK_BLOCK_DEEP(VAL_ROUTINE_ARGS(val));
			Queue_Mark_Routine_Deep(&VAL_ROUTINE(val));
			break;

		case REB_LIBRARY:
			MARK_LIB(VAL_LIB_HANDLE(val));
			QUEUE_MARK_BLOCK_DEEP(VAL_LIB_SPEC(val));
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

		default:
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

			assert(FALSE);
			Panic_Core(RP_DATATYPE+1, VAL_TYPE(val));
	}
}


/***********************************************************************
**
*/	static void Mark_Block_Deep_Core(REBSER *series)
/*
**		Mark all series reachable from the block.
**
***********************************************************************/
{
	REBCNT len;

	assert(!SERIES_FREED(series)); // should not be reaching freed series

	// We should have marked this series to keep it from being doubly
	// added already...
#ifndef NDEBUG
	if (!SERIES_GET_FLAG(series, SER_MARK)) Panic_Series(series);
#endif

	assert(SERIES_TAIL(series) < SERIES_REST(series)); // overflow

	for (len = 0; len < series->tail; len++) {
		REBVAL *val = BLK_SKIP(series, len);
		// We should never reach the end before len above.
		// Exception is the stack itself.
		assert(VAL_TYPE(val) != REB_END || (series == DS_Series));
		if (VAL_TYPE(val) == REB_FRAME) {
			assert(len == 0);
			ASSERT_FRAME(series);
			if ((series == VAL_SERIES(ROOT_ROOT)) || (series == Task_Series)) {
				// !!! Currently it is allowed that the root frames not
				// have a wordlist.  This distinct behavior accomodation is
				// not worth having the variance of behavior, but since
				// it's there for now... allow it for just those two.

				if(!VAL_FRM_WORDS(val))
					continue;
			}
		}
		Queue_Mark_Value_Deep(val);
	}

	assert(
		SERIES_WIDE(series) != sizeof(REBVAL)
		|| IS_END(BLK_SKIP(series, len))
		|| series == DS_Series
	);
}


/***********************************************************************
**
*/	ATTRIBUTE_NO_SANITIZE_ADDRESS static REBCNT Sweep_Series(void)
/*
**		Free all unmarked series.
**
**		Scans all series in all segments that are part of the
**		SERIES_POOL. Free series that have not been marked.
**
***********************************************************************/
{
	REBSEG	*seg;
	REBSER	*series;
	REBCNT  n;
	REBCNT	count = 0;

	for (seg = Mem_Pools[SERIES_POOL].segs; seg; seg = seg->next) {
		series = (REBSER *) (seg + 1);
		for (n = Mem_Pools[SERIES_POOL].units; n > 0; n--) {
			if (!SERIES_FREED(series)) {
				if (IS_FREEABLE(series)) {
					Free_Series(series);
					count++;
				} else
					SERIES_CLR_FLAG(series, SER_MARK);
			}
			series++;
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
		// Mark_Block_Deep_Core(), so must be refreshed on each loop.
		REBSER *series =
			cast(REBSER **, GC_Mark_Stack->data)[--GC_Mark_Stack->tail];

		// Drop the series we are processing off the tail, as we could be
		// queuing more of them (hence increasing the tail).
		cast(REBSER **, GC_Mark_Stack->data)[GC_Mark_Stack->tail] = NULL;

		Mark_Block_Deep_Core(series);
	}
}


/***********************************************************************
**
*/	REBCNT Recycle(void)
/*
**		Recycle memory no longer needed.
**
***********************************************************************/
{
	REBINT n;
	REBSER **sp;
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
	VAL_BLK_TERM(TASK_BUF_EMIT);
	VAL_BLK_TERM(TASK_BUF_WORDS);
//!!!	SET_END(BLK_TAIL(Save_Value_List));

	// Mark series stack (temp-saved series):
	sp = (REBSER **)GC_Protect->data;
	for (n = SERIES_TAIL(GC_Protect); n > 0; n--) {
        if (IS_BLOCK_SERIES(*sp))
            MARK_BLOCK_DEEP(*sp);
        else
            MARK_SERIES_ONLY(*sp);
		sp++; // can't increment inside macro arg, evaluated multiple times
	}

	// Mark all special series:
	sp = (REBSER **)GC_Series->data;
	for (n = SERIES_TAIL(GC_Series); n > 0; n--) {
        if (IS_BLOCK_SERIES(*sp))
            MARK_BLOCK_DEEP(*sp);
        else
            MARK_SERIES_ONLY(*sp);
		sp++; // can't increment inside macro arg, evaluated multiple times
	}

	// Mark the last MAX_SAFE "infant" series that were created.
	// We must assume that infant blocks are valid - that they contain
	// no partially valid datatypes (that are under construction).

	// !!! As written, this is much more likely to mask bugs than prevent
	// them.  (@HostileFork has a more formalized solution to this.)

	for (n = 0; n < MAX_SAFE_SERIES; n++) {
		REBSER *ser;
		if ((ser = GC_Infants[n])) {
			//Dump_Series(ser, "Safe Series");
            if (IS_BLOCK_SERIES(ser))
				MARK_BLOCK_DEEP(ser);
            else
                MARK_SERIES_ONLY(ser);
		} else break;
	}

	// Mark all root series:
	MARK_BLOCK_DEEP(VAL_SERIES(ROOT_ROOT));
	MARK_BLOCK_DEEP(Task_Series);

	// Mark all devices:
	Mark_Devices_Deep();

	// Mark function call frames:
	Mark_Call_Frames_Deep();

	count = Sweep_Routines(); // this needs to run before Sweep_Series(), because Routine has series with pointers, which can't be simply discarded by Sweep_Series

	count += Sweep_Series();
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
*/	void Save_Series(REBSER *series)
/*
***********************************************************************/
{
	if (SERIES_FULL(GC_Protect)) Extend_Series(GC_Protect, 8);
	((REBSER **)GC_Protect->data)[GC_Protect->tail++] = series;
}


/***********************************************************************
**
*/	void Guard_Series(REBSER *series)
/*
**		A list of protected series, managed by specific removal.
**
***********************************************************************/
{
	LABEL_SERIES(series, "guarded");
	if (SERIES_FULL(GC_Series)) Extend_Series(GC_Series, 8);
	((REBSER **)GC_Series->data)[GC_Series->tail++] = series;
}


/***********************************************************************
**
*/	void Loose_Series(REBSER *series)
/*
**		Remove a series from the protected list.
**
***********************************************************************/
{
	REBSER **sp;
	REBCNT n;

	LABEL_SERIES(series, "unguarded");
	sp = (REBSER **)GC_Series->data;
	for (n = 0; n < SERIES_TAIL(GC_Series); n++) {
		if (sp[n] == series) {
			Remove_Series(GC_Series, n, 1);
			break;
		}
	}
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
	GC_Last_Infant = 0;		// Keep the last N series safe from GC.
	GC_Infants = ALLOC_ARRAY(REBSER*, MAX_SAFE_SERIES + 2); // extra

	Prior_Expand = ALLOC_ARRAY(REBSER*, MAX_EXPAND_LIST);
	Prior_Expand[0] = (REBSER*)1;

	// Temporary series protected from GC. Holds series pointers.
	GC_Protect = Make_Series(15, sizeof(REBSER *), MKS_NONE);
	KEEP_SERIES(GC_Protect, "gc protected");

	// The marking queue used in lieu of recursion to ensure that deeply
	// nested structures don't cause the C stack to overflow.
	GC_Mark_Stack = Make_Series(100, sizeof(REBSER *), MKS_NONE);
	TERM_SERIES(GC_Mark_Stack);
	KEEP_SERIES(GC_Mark_Stack, "gc mark stack");

	GC_Series = Make_Series(60, sizeof(REBSER *), MKS_NONE);
	KEEP_SERIES(GC_Series, "gc guarded");
}
