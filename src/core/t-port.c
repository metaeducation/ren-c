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
**  Module:  t-port.c
**  Summary: port datatype
**  Section: datatypes
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

#include "sys-core.h"


/***********************************************************************
**
*/	REBINT CT_Port(REBVAL *a, REBVAL *b, REBINT mode)
/*
***********************************************************************/
{
	if (mode < 0) return -1;
	return VAL_OBJ_FRAME(a) == VAL_OBJ_FRAME(b);
}


/***********************************************************************
**
*/	REBFLG MT_Port(REBVAL *out, REBVAL *data, enum Reb_Kind type)
/*
***********************************************************************/
{
	return FALSE;
}


/***********************************************************************
**
*/	REBTYPE(Port)
/*
***********************************************************************/
{
	REBVAL *value = D_ARG(1);
	REBVAL *arg = DS_ARGC > 1 ? D_ARG(2) : NULL;

	switch (action) {

	case A_READ:
	case A_WRITE:
	case A_QUERY:
	case A_OPEN:
	case A_CREATE:
	case A_DELETE:
	case A_RENAME:
		// !!! We are going to "re-apply" the call frame with routines that
		// are going to read the D_ARG(1) slot *implicitly* regardless of
		// what value points to.  And dodgily, we must also make sure the
		// output is set.  Review.
		if (!IS_PORT(value)) {
			Make_Port(D_OUT, value);
			*D_ARG(1) = *D_OUT;
			value = D_ARG(1);
		} else
			*D_OUT = *value;
	case A_UPDATE:
	default:
		return Do_Port_Action(call_, VAL_PORT(value), action); // Result on stack

	case A_REFLECT:
		return T_Object(call_, action);

	case A_MAKE:
		if (!IS_DATATYPE(value)) raise Error_Bad_Make(REB_PORT, value);
		Make_Port(value, arg);
		break;

	case A_TO:
		if (!(IS_DATATYPE(value) && IS_OBJECT(arg)))
			raise Error_Bad_Make(REB_PORT, arg);
		value = arg;
		VAL_SET(value, REB_PORT);
		break;
	}

	*D_OUT = *value;
	return R_OUT;
}
