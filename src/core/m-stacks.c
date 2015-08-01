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
	assert((DS_Index >= -1) && (DS_Frame_Index >= -1));
	assert(DS_Index >= DS_Frame_Index);
	if (DS_Frame_Index != -1) {
		assert(PRIOR_DSF(DS_Frame_Index) < DS_Frame_Index);
		assert(ANY_FUNC(DSF_FUNC(DS_Frame_Index)));
		assert(ANY_BLOCK(DSF_WHERE(DS_Frame_Index)));
		ASSERT_BLK(VAL_SERIES(DSF_WHERE(DS_Frame_Index)));
	}

	return &DS_Frame_Index;
}

#endif
