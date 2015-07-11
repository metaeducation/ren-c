/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2014 Atronix Engineering, Inc.
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
**  Module:  t-library.c
**  Summary: External Library Support
**  Section: datatypes
**  Author:  Shixin Zeng
**  Notes:
**
***********************************************************************/

#include "sys-core.h"

/***********************************************************************
**
*/	REBINT CT_Library(REBVAL *a, REBVAL *b, REBINT mode)
/*
***********************************************************************/
{
	//RL_Print("%s, %d\n", __func__, __LINE__);
	if (mode >= 0) {
		return VAL_LIB_HANDLE(a) == VAL_LIB_HANDLE(b);
	}
	return -1;
}

/***********************************************************************
**
*/	REBTYPE(Library)
/*
***********************************************************************/
{
	REBVAL *val;
	REBVAL *arg;
	REBSTU *strut;
	REBVAL *ret;

	arg = D_ARG(2);
	val = D_ARG(1);
	strut = 0;

	ret = DS_RETURN;
	// unary actions
	switch(action) {
		case A_MAKE:
			//RL_Print("%s, %d, Make library action\n", __func__, __LINE__);
		case A_TO:
			if (!IS_DATATYPE(val)) {
				Trap_Types_DEAD_END(RE_EXPECT_VAL, REB_LIBRARY, VAL_TYPE(val));
			}
			if (!IS_FILE(arg)) {
				Trap_Types_DEAD_END(RE_EXPECT_VAL, REB_FILE, VAL_TYPE(arg));
			} else {
				REBCNT len = VAL_LEN(arg);
				void *lib = NULL;
				REBCNT error = 0;
				REBSER *path = Value_To_OS_Path(arg, FALSE);
				lib = OS_OPEN_LIBRARY(cast(REBCHR*, SERIES_DATA(path)), &error);
				if (!lib) {
					Trap_Make_DEAD_END(REB_LIBRARY, arg);
				}
				VAL_LIB_SPEC(ret) = Make_Block(1);
				Append_Val(VAL_LIB_SPEC(ret), arg);
				VAL_LIB_HANDLE(ret) = (REBLHL*)Make_Node(LIB_POOL);
				VAL_LIB_FD(ret) = lib;
				USE_LIB(VAL_LIB_HANDLE(ret));
				OPEN_LIB(VAL_LIB_HANDLE(ret));
				SET_TYPE(ret, REB_LIBRARY);
			}
			break;
		case A_CLOSE:
			OS_CLOSE_LIBRARY(VAL_LIB_FD(val));
			CLOSE_LIB(VAL_LIB_HANDLE(val));
			break;
		default:
			Trap_Action_DEAD_END(REB_LIBRARY, action);
	}
	return R_RET;
}
