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
**		insertions ahead of time.  If a new series is to be created,
**		it will not waste space or time on expansions.  If a series
**		is to be inserted into as a target, the proper size gap for
**		the insertion can be opened up exactly once without any
**		need for repeatedly shuffling on individual insertions.
**
**		Beyond that purpose, the data stack can also be used as a
**		place to store a value to protect it from the garbage
**		collector.  The stack must be balanced in the case of success
**		when a native or action runs, but if a Trap() is called then
**		the stack will be automatically balanced.
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


// "Data Stack Frame" indexes into Rebol's data stack at the location where
// the block of information about a function call begins.  It starts with the
// location where the return value is written, and has other properties (like
// the REBVAL of the function being called itself) up to the values that are
// computed arguments to the function.

// !!! Note that terminology-wise, the slot in the frame that used to be
// called DSF_RETURN is now called DSF_OUT.  It is the first element in
// the frame in the data-stack implementation, because when the stack is
// "dropped" back to the point where the call was made, it is what is on
// the top of the stack.  But in StableStack, this can be a pointer to any
// address, as function calls can be told to write their output anywhere.
// (and the REBVAL* parameter to the replacement for Do_Core() is called "out"
// so it makes sense in that way, too.)
//
// !!! Vis a vis, concordantly...DSF_RETURN is reserved for the definitionally
// scoped return function built for the specific call the frame represents.


#define DSF_SIZE		5					// from DSF to ARGS-1

// where to write return value (via a handle indirection for now)
#define DSF_OUT(d) \
	cast(REBVAL*, VAL_HANDLE_DATA(DS_AT((d) + 1)))

#define PRIOR_DSF(d) \
	VAL_INT32(DS_AT((d) + 2))

#define DSF_WHERE(d)	DS_AT((d) + 3)	// block and index of execution
#define DSF_LABEL(d)	DS_AT((d) + 4)	// func word backtrace
#define DSF_FUNC(d)		DS_AT((d) + 5)	// function value saved
#define DSF_RETURN(d)	coming@soon			// return func linked to this call
#define DSF_ARG(d,n)	DS_AT((d) + DSF_SIZE + FIRST_PARAM_INDEX + (n) - 1)


#ifdef STRESS
	// In a "stress checked" debug mode, every time the DSF is accessed we
	// can verify that it is well-formed.
	#define DSF (*DSF_Stress())
	#define SET_DSF(ds) \
		(DS_Frame_Index = (ds), cast(void, DSF_Stress()))
#else
	// Normal builds just use DS_Frame_Index directly
	#define DSF (DS_Frame_Index + 0) // avoid assignment to DSF via + 0
	#define SET_DSF(ds) \
		(DS_Frame_Index = (ds))
#endif

// !!! Ultimately the DSF will be done some other way, but for now this is
// how to indicate there is no stack frame.
#define DSF_NONE MIN_I32


// Reference from ds that points to current return value:
#define D_OUT			DSF_OUT(call_->dsf)
#define D_ARG(n)		DSF_ARG(call_->dsf, (n))
#define D_REF(n)		(!IS_NONE(D_ARG(n)))

// Reference from current DSF index:
#define DS_ARG_BASE		(DSF+DSF_SIZE)
#define DS_ARG(n)		DSF_ARG(DSF, n)
#define DS_REF(n)		(!IS_NONE(DS_ARG(n)))
#define DS_ARGC			(DSP-DS_ARG_BASE)
