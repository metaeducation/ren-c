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
	DS_Base = BLK_HEAD(DS_Series);
	DSP = -1;
	SET_DSF(DSF_NONE);
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
	DS_TERMINATE; // !!! Unnecessary when DS_Series goes legit...
	Insert_Series(
		DS_Series, SERIES_TAIL(DS_Series), cast(const REBYTE*, values), length
	);
	DSP += length;
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

	DSP = dsp_start;
}


#ifdef STRESS

/***********************************************************************
**
*/	REBINT* DSF_Stress(void)
/*
**		If there is an issue in testing where the function call frame
**		is found to contain bad information at some point, this
**		can be used in a "stress mode" to check when it went bad.
**		DSF is a macro which is changed to call this function (and
**		then dereference the returned pointer to get an LValue).
**
**		More checks are possible, but the call stack model will be
**		changing to not use the data stack (to an implementation with
**		stable value pointers for the argumetns), so this is a
**		placeholder until that implementation is committed.
**
***********************************************************************/
{
	assert(DS_Index >= -1);
	if (DS_Frame_Index != DSF_NONE) {
		assert(DS_Frame_Index >= -1 && DS_Index >= DS_Frame_Index);
		assert(PRIOR_DSF(DS_Frame_Index) < DS_Frame_Index);
		assert(ANY_FUNC(DSF_FUNC(DS_Frame_Index)));
		assert(ANY_BLOCK(DSF_WHERE(DS_Frame_Index)));
		ASSERT_BLK(VAL_SERIES(DSF_WHERE(DS_Frame_Index)));
	}

	return &DS_Frame_Index;
}

#endif
