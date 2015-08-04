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
**  Module:  m-stack.c
**  Summary: data and function call stack implementation
**  Section: memory
**  Notes:
**
***********************************************************************/

#include "sys-core.h"


/***********************************************************************
**
*/	void Init_Data_Stack(REBCNT size)
/*
***********************************************************************/
{
	DS_Series = Make_Block(size);
	Set_Root_Series(TASK_STACK, DS_Series, "data stack"); // uses special GC
	CS_Running = NULL;
	CS_Top = NULL;
}


/***********************************************************************
**
*/	void Push_Stack_Values(const REBVAL *values, REBINT length)
/*
**		Pushes sequential values from a series onto the stack all
**		in one go.  All of this needs review in terms of whether
**		things like COMPOSE should be using arbitrary stack pushes
** 		in the first place or if it should not pile up the stack
**		like this.
**
**		!!! Notably simple implementation, just hammering out the
**		client interfaces that made sequential stack memory assumptions.
**
***********************************************************************/
{
	Insert_Series(
		DS_Series, SERIES_TAIL(DS_Series), cast(const REBYTE*, values), length
	);
}


/***********************************************************************
**
*/	void Pop_Stack_Values(REBVAL *out, REBINT dsp_start, REBOOL into)
/*
**		Pop_Stack_Values computed values from the stack into the series
**		specified by "into", or if into is NULL then store it as a
**		block on top of the stack.  (Also checks to see if into
**		is protected, and will trigger a trap if that is the case.)
**
**		Protocol for /INTO is to set the position to the tail.
**
***********************************************************************/
{
	REBSER *series;
	REBCNT len = DSP - dsp_start;
	REBVAL *values = BLK_SKIP(DS_Series, dsp_start + 1);

	if (into) {
		assert(ANY_BLOCK(out));
		series = VAL_SERIES(out);
		if (IS_PROTECT_SERIES(series)) Trap(RE_PROTECTED);
		VAL_INDEX(out) = Insert_Series(
			series, VAL_INDEX(out), cast(REBYTE*, values), len
		);
	}
	else {
		series = Copy_Values(values, len);
		Set_Block(out, series);
	}

	DS_DROP_TO(dsp_start);
}


/***********************************************************************
**
*/	void Expand_Stack(REBCNT amount)
/*
**		Expand the datastack. Invalidates any references to stack
**		values, so code should generally use stack index integers,
**		not pointers into the stack.
**
***********************************************************************/
{
	if (SERIES_REST(DS_Series) >= STACK_LIMIT) Trap(RE_STACK_OVERFLOW);
	Extend_Series(DS_Series, amount);
	Debug_Fmt(cs_cast(BOOT_STR(RS_STACK, 0)), DSP, SERIES_REST(DS_Series));
}


/***********************************************************************
**
*/	struct Reb_Call *Make_Call(REBVAL *out, REBSER *block, REBCNT index, const REBVAL *label, const REBVAL *func)
/*
**		Create a function call frame as defined in stack.h.
**
**		We do not push the frame at the same time we create it,
**		because we need to fulfill its arguments in the caller's
**		frame before we actually invoke the function.
**
***********************************************************************/
{
	REBCNT num_vars = VAL_FUNC_NUM_WORDS(func);

	// Variable-sized allocation (would ideally use chunking)
	struct Reb_Call *call = cast(struct Reb_Call*, ALLOC_ARRAY(REBYTE*,
		sizeof(struct Reb_Call) + sizeof(REBVAL) * num_vars
	));

	// Even though we can't push this stack frame to the CSP yet, it
	// still needs to be considered for GC and freeing in case of a
	// trap.  In a recursive DO we can get many pending frames before
	// we come back to actually putting the topmost one in effect.
	// !!! Better design for call frame stack coming.
	call->prior = CS_Top;
	CS_Top = call;

#if !defined(NDEBUG)
	call->pending = TRUE;
#endif

	call->out = out;

	assert(ANY_FUNC(func));
	call->func = *func;

	assert(block); // Don't accept NULL series
	VAL_SET(&call->where, REB_BLOCK);
	VAL_SERIES(&call->where) = block;
	VAL_INDEX(&call->where) = index;

	// Save symbol describing the function (if we called this as the result of
	// a word or path lookup)
	if (!label) {
		// !!! When a function was not invoked through looking up a word to
		// (or a word in a path) to use as a label, there were three different
		// alternate labels used.  One was SYM__APPLY_, another was
		// ROOT_NONAME, and another was to be the type of the function being
		// executed.  None are fantastic, but we do the type for now.
		call->label = *Get_Type_Word(VAL_TYPE(func));
	}
	else {
		assert(IS_WORD(label));
		call->label = *label;
	}
	// !!! Not sure why this is needed; seems the label word should be unbound
	// if anything...
	VAL_WORD_FRAME(&call->label) = VAL_FUNC_WORDS(func);

	// Fill call frame's args with default of NONE!.  Have to do this in
	// advance because refinement filling often skips around; if you have
	// 'foo: func [/bar a /baz b] [...]' and you call foo/baz, it will jump
	// ahead to process positions 3 and 4, then determine there are no more
	// refinements, and not revisit slots 1 and 2.
	//
	// It's also necessary because the slots must be GC-safe values, in case
	// there is a Recycle() during argument fulfillment.

	call->num_vars = num_vars;
	{
		REBCNT var_index;
		for (var_index = 0; var_index < num_vars; var_index++)
			SET_NONE(&call->vars[var_index]);
	}

	return call;
}


/***********************************************************************
**
*/	void Free_Call(struct Reb_Call* call)
/*
***********************************************************************/
{
	assert(call == CS_Top);
	CS_Top = call->prior;

	// See notes on why there is a -1 here, to allow for safe compilation
	// in C++ where vars cannot be a zero size array
	Free_Mem(call, sizeof(struct Reb_Call) + sizeof(REBVAL) * call->num_vars);
}


#ifdef STRESS

/***********************************************************************
**
*/	struct Reb_Call *DSF_Stress(void)
/*
**		If there is an issue in testing where the function call frame
**		is found to contain bad information at some point, this
**		can be used in a "stress mode" to check when it went bad.
**		DSF is a macro which is changed to call this function (and
**		then dereference the returned pointer to get an LValue).
**
**		!!! This was used when the call stack frames were on the
**		data stack and could get intermingled; less relevant now.
**
***********************************************************************/
{
	assert(DSP >= -1);
	if (CS_Running) {
		REBCNT index;
		assert(ANY_FUNC(DSF_FUNC(CS_Running)));
		assert(ANY_BLOCK(DSF_WHERE(CS_Running)));
		ASSERT_BLK(VAL_SERIES(DSF_WHERE(CS_Running)));
	}

	return CS_Running;
}

#endif
