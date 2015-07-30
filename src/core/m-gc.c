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
REBVAL *N_watch(REBFRM *frame, REBVAL **inter_block)
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

static void Mark_Value_Deep_Core(REBVAL *val);

static void Mark_Block_Deep_Core(REBSER *series);

static void Mark_Series_Only_Debug(REBSER *ser);

/***********************************************************************
**
*/	static void Push_Mark_Block_Deep_Core(REBSER *series)
/*
**		Push the series into the deferred stack to be processed later
**		with Process_Mark_Stack().  We go ahead and set this series mark
**		because it's now "spoken for".  (Even though we haven't marked
**		its actual dependencies yet, we want to prevent it from being
**		wastefully stacked multiple times by another reference that
**		would still see it as "unmarked".)
**
***********************************************************************/
{
	assert(IS_BLOCK_SERIES(series));

	assert(!SERIES_GET_FLAG(series, SER_MARK));
	SERIES_SET_FLAG(series, SER_MARK);

	// "External" series are not marked or managed by the GC.
	// !!! Should the REBSER be GC'd, but just their data pointers left
	// alone and not freed?  What's the real goal? --@HF
	if (IS_EXT_SERIES(series))
		return;

	// Add series to the end of the mark stack series and update terminator
	if (SERIES_FULL(GC_Mark_Stack)) Extend_Series(GC_Mark_Stack, 8);
	cast(REBSER **, GC_Mark_Stack->data)[GC_Mark_Stack->tail++] = series;
	cast(REBSER **, GC_Mark_Stack->data)[GC_Mark_Stack->tail] = NULL;
}

static void Process_Mark_Stack(void);

#ifndef NDEBUG
	static REBOOL in_mark = FALSE;
#endif

// NOTE: The following macros uses S parameter multiple times, hence if S has
// side effects this will run that side-effect multiply.

// Non-Queued form for marking series.  Used for marking a *root set item*,
// don't recurse from within Mark_Value/Mark_Gob/Mark_Block_Deep/etc.

#define MARK_SERIES_DEEP(s) \
	do { \
		assert(!in_mark); \
		if (!SERIES_GET_FLAG((s), SER_MARK)) { \
			SERIES_SET_FLAG((s), SER_MARK); \
			if (IS_BLOCK_SERIES(s)) { \
				Mark_Block_Deep_Core(s); \
				Process_Mark_Stack(); \
			} \
		} \
	} while (0)


// Deferred form for marking series that prevents potentially overflowing the
// C execution stack.

#define PUSH_MARK_BLOCK_DEEP(s) \
	do { \
		assert(IS_BLOCK_SERIES(s)); \
		if (!SERIES_GET_FLAG((s), SER_MARK)) { \
			Push_Mark_Block_Deep_Core(s); \
		} \
	} while (0)

#ifdef NDEBUG
	#define MARK_SERIES_ONLY(s) SERIES_SET_FLAG((s), SER_MARK)
#else
	#define MARK_SERIES_ONLY(s) Mark_Series_Only_Debug(s)
#endif

// "Unword" blocks contain REBWRS-style words, which have type information
// instead of a binding.  They shouldn't have any other types in them so we
// don't need to mark deep...BUT doesn't hurt to check in debug builds!
#define MARK_UNWORDS_BLOCK(s) \
	do { \
		ASSERT_UNWORDS_BLOCK(s); \
		MARK_SERIES_ONLY(s); \
	} while (0)


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
*/	static void Mark_Gob(REBGOB *gob)
/*
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
			Mark_Gob(*pane);
	}

	if (GOB_PARENT(gob)) Mark_Gob(GOB_PARENT(gob));

	if (GOB_CONTENT(gob)) {
		if (GOB_TYPE(gob) >= GOBT_IMAGE && GOB_TYPE(gob) <= GOBT_STRING)
			SERIES_SET_FLAG(GOB_CONTENT(gob), SER_MARK);
		else if (GOB_TYPE(gob) >= GOBT_DRAW && GOB_TYPE(gob) <= GOBT_EFFECT)
			PUSH_MARK_BLOCK_DEEP(GOB_CONTENT(gob));
	}

	if (GOB_DATA(gob) && GOB_DTYPE(gob) && GOB_DTYPE(gob) != GOBD_INTEGER)
		PUSH_MARK_BLOCK_DEEP(GOB_DATA(gob));
}


/***********************************************************************
**
*/	static void Mark_Struct_Field(REBSTU *stu, struct Struct_Field *field)
/*
***********************************************************************/
{
	if (field->type == STRUCT_TYPE_STRUCT) {
		unsigned int len = 0;
		REBSER *field_fields = field->fields;

		PUSH_MARK_BLOCK_DEEP(field_fields);
		PUSH_MARK_BLOCK_DEEP(field->spec);

		for (len = 0; len < field_fields->tail; len++) {
			Mark_Struct_Field(
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
				Mark_Value_Deep_Core(data);
		}
	}
	else {
		// ignore primitive datatypes
	}
}


/***********************************************************************
**
*/	static void Mark_Struct(REBSTU *stu)
/*
***********************************************************************/
{
	unsigned int len = 0;
	REBSER *series = NULL;

	// The spec is the only ANY-BLOCK! in the struct
	PUSH_MARK_BLOCK_DEEP(stu->spec);

	MARK_SERIES_ONLY(stu->fields);
	MARK_SERIES_ONLY(STRUCT_DATA_BIN(stu));

	assert(!IS_EXT_SERIES(stu->data));
	assert(SERIES_TAIL(stu->data) == 1);
	MARK_SERIES_ONLY(stu->data);

	series = stu->fields;
	for (len = 0; len < series->tail; len++) {
		struct Struct_Field *field = cast(struct Struct_Field*,
			SERIES_SKIP(series, len)
		);

		Mark_Struct_Field(stu, field);
	}
}


/***********************************************************************
**
*/	static void Mark_Routine(REBROT *rot)
/*
***********************************************************************/
{
	PUSH_MARK_BLOCK_DEEP(ROUTINE_SPEC(rot));
	ROUTINE_SET_FLAG(ROUTINE_INFO(rot), ROUTINE_MARK);

	MARK_SERIES_ONLY(ROUTINE_FFI_ARG_TYPES(rot));
	PUSH_MARK_BLOCK_DEEP(ROUTINE_FFI_ARG_STRUCTS(rot));
	MARK_SERIES_ONLY(ROUTINE_EXTRA_MEM(rot));

	if (IS_CALLBACK_ROUTINE(ROUTINE_INFO(rot))) {
		if (FUNC_BODY(&CALLBACK_FUNC(rot))) {
			PUSH_MARK_BLOCK_DEEP(FUNC_BODY(&CALLBACK_FUNC(rot)));
			PUSH_MARK_BLOCK_DEEP(FUNC_SPEC(&CALLBACK_FUNC(rot)));
			SERIES_SET_FLAG(FUNC_ARGS(&CALLBACK_FUNC(rot)), SER_MARK);
		}
		else {
			// may be null if called before the callback! is fully constructed
		}
	} else {
		if (ROUTINE_GET_FLAG(ROUTINE_INFO(rot), ROUTINE_VARARGS)) {
			if (ROUTINE_FIXED_ARGS(rot))
				PUSH_MARK_BLOCK_DEEP(ROUTINE_FIXED_ARGS(rot));

			if (ROUTINE_ALL_ARGS(rot))
				PUSH_MARK_BLOCK_DEEP(ROUTINE_ALL_ARGS(rot));
		}

		if (ROUTINE_LIB(rot))
			MARK_LIB(ROUTINE_LIB(rot));
		else {
			// may be null if called before the routine! is fully constructed
		}

		if (ROUTINE_RVALUE(rot).spec)
			Mark_Struct(&ROUTINE_RVALUE(rot));
	}
}


/***********************************************************************
**
*/	static void Mark_Event(REBVAL *value)
/*
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
		PUSH_MARK_BLOCK_DEEP(cast(REBSER*, VAL_EVENT_SER(value)));
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
				PUSH_MARK_BLOCK_DEEP(cast(REBSER*, req->port));
			req = req->next;
		}
	}
}


/***********************************************************************
**
*/ static void Mark_Devices(void)
/*
**		Mark all devices. Search for pending requests.
**
**		This should be called at the top level, and not from inside a
**		Process_Mark_Stack().
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
				MARK_SERIES_DEEP(cast(REBSER*, req->port));
	}
}


/***********************************************************************
**
*/	static void Mark_Value_Deep_Core(REBVAL *val)
/*
***********************************************************************/
{
	REBSER *ser = NULL;

	switch (VAL_TYPE(val)) {
		case REB_UNSET:
		case REB_TYPESET:
		case REB_HANDLE:
			break;

		case REB_DATATYPE:
			// Type spec is allowed to be NULL.  See %typespec.r file
			if (VAL_TYPE_SPEC(val))
				PUSH_MARK_BLOCK_DEEP(VAL_TYPE_SPEC(val));
			break;

		case REB_ERROR:
			if (VAL_ERR_NUM(val) < RE_THROW_MAX) {
				// If it has no error object, then it is a THROW.  A GC of a
				// THROW value is invalid because it contains temporary values
				// on the stack that could be above the current DSP
				// (where the THROW was done).

				// !!! It's not clear if this should crash or not.
				// Aggressive Recycle() forces this to happen.

				// Panic(RP_THROW_IN_GC);
			} else
				PUSH_MARK_BLOCK_DEEP(VAL_ERR_OBJECT(val));
			break;

		case REB_TASK: // not yet implemented
			break;

		case REB_FRAME:
			// Mark special word list. Contains no pointers because
			// these are special word bindings (to typesets if used).
			MARK_UNWORDS_BLOCK(VAL_FRM_WORDS(val));
			if (VAL_FRM_SPEC(val))
				PUSH_MARK_BLOCK_DEEP(VAL_FRM_SPEC(val));
			// !!! See code below for ANY-WORD! which also deals with FRAME!
			break;

		case REB_PORT:
		case REB_OBJECT:
			// Objects currently only have a FRAME, but that protects the
			// keys wordlist via the FRAME! value in the first slot (which
			// will be visited along with mapped values via this deep mark)
			PUSH_MARK_BLOCK_DEEP(VAL_OBJ_FRAME(val));
			break;

		case REB_MODULE:
			// A module is an object with an optional body (they currently
			// do not use the body)
			PUSH_MARK_BLOCK_DEEP(VAL_OBJ_FRAME(val));
			if (VAL_MOD_BODY(val))
				PUSH_MARK_BLOCK_DEEP(VAL_MOD_BODY(val));
			break;

		case REB_FUNCTION:
		case REB_COMMAND:
		case REB_CLOSURE:
		case REB_REBCODE:
			PUSH_MARK_BLOCK_DEEP(VAL_FUNC_BODY(val));
		case REB_NATIVE:
		case REB_ACTION:
		case REB_OP:
			PUSH_MARK_BLOCK_DEEP(VAL_FUNC_SPEC(val));
			MARK_UNWORDS_BLOCK(VAL_FUNC_ARGS(val));
			break;

		case REB_WORD:	// (and also used for function STACK backtrace frame)
		case REB_SET_WORD:
		case REB_GET_WORD:
		case REB_LIT_WORD:
		case REB_REFINEMENT:
		case REB_ISSUE:
			// Special word used in word frame, stack, or errors:
            if (VAL_GET_OPT(val, OPTS_UNWORD)) break;

			ser = VAL_WORD_FRAME(val);
			if (ser) {
				// Word is bound, so mark its context (which may be a FRAME!
				// series or an identifying function word series).  All
				// bound words should keep their contexts from being GC'd...
				// even stack-relative contexts for functions.

				REBVAL *first;
				assert(SERIES_LEN(ser) > 0);
				first = BLK_HEAD(ser);

				if (IS_FRAME(first)) {
					// It's referring to an OBJECT!-style FRAME, where the
					// first element is a FRAME! containing the word keys
					// and the rest of the elements are the data values
					PUSH_MARK_BLOCK_DEEP(ser);
				}
				else {
					// It's referring to a FUNCTION!'s identifying series,
					// which should just be a list of UNWORDs.
					MARK_UNWORDS_BLOCK(ser);
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

			PUSH_MARK_BLOCK_DEEP(ser);
			break;

		case REB_MAP:
			ser = VAL_SERIES(val);
			PUSH_MARK_BLOCK_DEEP(ser);
			if (ser->extra.series)
				MARK_SERIES_ONLY(ser->extra.series);
			break;

		case REB_CALLBACK:
		case REB_ROUTINE:
			PUSH_MARK_BLOCK_DEEP(VAL_ROUTINE_SPEC(val));
			PUSH_MARK_BLOCK_DEEP(VAL_ROUTINE_ARGS(val));
			Mark_Routine(&VAL_ROUTINE(val));
			break;

		case REB_LIBRARY:
			MARK_LIB(VAL_LIB_HANDLE(val));
			PUSH_MARK_BLOCK_DEEP(VAL_LIB_SPEC(val));
			break;

		case REB_STRUCT:
			Mark_Struct(&VAL_STRUCT(val));
			break;

		case REB_GOB:
			Mark_Gob(VAL_GOB(val));
			break;

		case REB_EVENT:
			Mark_Event(val);
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
		Mark_Value_Deep_Core(val);
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
			SKIP_WALL(series);
			MUNG_CHECK(SERIES_POOL, series, sizeof(*series));
			if (!SERIES_FREED(series)) {
				if (IS_FREEABLE(series)) {
					Free_Series(series);
					count++;
				} else
					SERIES_CLR_FLAG(series, SER_MARK);
			}
			series++;
			SKIP_WALL(series);
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
#ifdef MUNGWALL
			gob = (gob *) (((REBYTE *)s)+MUNG_SIZE);
			MUNG_CHECK(GOB_POOL, gob, sizeof(*gob));
#endif
			if (IS_GOB_USED(gob)) {
				if (IS_GOB_MARK(gob))
					UNMARK_GOB(gob);
				else {
					Free_Gob(gob);
					count++;
				}
			}
			gob++;
#ifdef MUNGWALL
			gob = (gob *) (((REBYTE *)s)+MUNG_SIZE);
#endif
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
			SKIP_WALL(lib);
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
			SKIP_WALL(info);
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
*/	static void Process_Mark_Stack(void)
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

#include <stdio.h>

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
	DS_TERMINATE; // Update data stack tail
//	SET_END(DS_NEXT);
	VAL_BLK_TERM(TASK_BUF_EMIT);
	VAL_BLK_TERM(TASK_BUF_WORDS);
//!!!	SET_END(BLK_TAIL(Save_Value_List));

	// Mark series stack (temp-saved series):
	sp = (REBSER **)GC_Protect->data;
	for (n = SERIES_TAIL(GC_Protect); n > 0; n--) {
		MARK_SERIES_DEEP(*sp);
		sp++; // can't increment inside macro arg, evaluated multiple times
	}

	// Mark all special series:
	sp = (REBSER **)GC_Series->data;
	for (n = SERIES_TAIL(GC_Series); n > 0; n--) {
		MARK_SERIES_DEEP(*sp);
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
			MARK_SERIES_DEEP(ser);
		} else break;
	}

	// Mark all root series:
	MARK_SERIES_DEEP(VAL_SERIES(ROOT_ROOT));
	MARK_SERIES_DEEP(Task_Series);

	// Mark all devices:
	Mark_Devices();

	count = Sweep_Routines(); // this needs to run before Sweep_Series(), because Routine has series with pointers, which can't be simply discarded by Sweep_Series

	count += Sweep_Series();
	count += Sweep_Gobs();
	count += Sweep_Libs();

	CHECK_MEMORY(4);

	// Compute new stats:
	PG_Reb_Stats->Recycle_Series = Mem_Pools[SERIES_POOL].free - PG_Reb_Stats->Recycle_Series;
	PG_Reb_Stats->Recycle_Series_Total += PG_Reb_Stats->Recycle_Series;
	PG_Reb_Stats->Recycle_Prior_Eval = Eval_Cycles;

	// Reset stack to prevent invalid MOLD access:
	RESET_TAIL(DS_Series);

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

#include <stdio.h>

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
	KEEP_SERIES(GC_Mark_Stack, "gc mark queue");

	GC_Series = Make_Series(60, sizeof(REBSER *), MKS_NONE);
	KEEP_SERIES(GC_Series, "gc guarded");
}
