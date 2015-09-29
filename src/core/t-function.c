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
**  Module:  t-function.c
**  Summary: function related datatypes
**  Section: datatypes
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

#include "sys-core.h"

static REBOOL Same_Func(REBVAL *val, REBVAL *arg)
{
	if (VAL_TYPE(val) == VAL_TYPE(arg) &&
		VAL_FUNC_SPEC(val) == VAL_FUNC_SPEC(arg) &&
		VAL_FUNC_PARAMLIST(val) == VAL_FUNC_PARAMLIST(arg) &&
		VAL_FUNC_CODE(val) == VAL_FUNC_CODE(arg)) return TRUE;
	return FALSE;
}


/***********************************************************************
**
*/	REBINT CT_Function(REBVAL *a, REBVAL *b, REBINT mode)
/*
***********************************************************************/
{
	if (mode >= 0) return Same_Func(a, b);
	return -1;
}


/***********************************************************************
**
*/	REBSER *As_Typesets(REBSER *types)
/*
***********************************************************************/
{
	REBVAL *val;

	types = Copy_Array_At_Shallow(types, 1);
	for (val = BLK_HEAD(types); NOT_END(val); val++) {
		SET_TYPE(val, REB_TYPESET);
	}
	return types;
}


/***********************************************************************
**
*/	REBFLG MT_Function(REBVAL *out, REBVAL *data, REBCNT type)
/*
***********************************************************************/
{
	return Make_Function(out, type, data);
}


/***********************************************************************
**
*/	REBTYPE(Function)
/*
***********************************************************************/
{
	REBVAL *value = D_ARG(1);
	REBVAL *arg = DS_ARGC > 1 ? D_ARG(2) : NULL;

	switch (action) {
	case A_TO:
		// `to function! foo` is meaningless (and should not be given meaning,
		// because `to function! [print "DOES exists for this, for instance"]`
		break;

	case A_MAKE:
		if (!IS_DATATYPE(value)) raise Error_Invalid_Arg(value);

		// Make_Function checks for an `[[args] [body]]`-style make argument
		if (!Make_Function(D_OUT, VAL_TYPE_KIND(value), arg))
			raise Error_Bad_Make(VAL_TYPE_KIND(value), arg);
		return R_OUT;

	case A_COPY:
		// Functions can modify their bodies while running, effectively
		// accruing state which you may want to snapshot as a copy.
		Copy_Function(D_OUT, value);
		return R_OUT;

	case A_REFLECT:
		switch (What_Reflector(arg)) {
		case OF_WORDS:
			Val_Init_Block(D_OUT, List_Func_Words(value));
			return R_OUT;

		case OF_BODY:
			switch (VAL_TYPE(value)) {
			case REB_FUNCTION:
				Val_Init_Block(
					D_OUT, Copy_Array_Deep_Managed(VAL_FUNC_BODY(value))
				);
				// See CC#2221 for why function body copies don't unbind locals
				return R_OUT;

			case REB_CLOSURE:
				Val_Init_Block(
					D_OUT, Copy_Array_Deep_Managed(VAL_FUNC_BODY(value))
				);
				// See CC#2221 for why closure body copies have locals unbound
				Unbind_Values_Core(
					VAL_BLK_HEAD(D_OUT), VAL_FUNC_PARAMLIST(value), TRUE
				);
				return R_OUT;

			case REB_NATIVE:
			case REB_COMMAND:
			case REB_ACTION:
				return R_NONE;
			}
			break;

		case OF_SPEC:
			Val_Init_Block(
				D_OUT, Copy_Array_Deep_Managed(VAL_FUNC_SPEC(value))
			);
			Unbind_Values_Deep(VAL_BLK_HEAD(value));
			return R_OUT;

		case OF_TYPES:
			Val_Init_Block(D_OUT, As_Typesets(VAL_FUNC_PARAMLIST(value)));
			return R_OUT;

		case OF_TITLE:
			arg = BLK_HEAD(VAL_FUNC_SPEC(value));
			while (NOT_END(arg) && !IS_STRING(arg) && !IS_WORD(arg))
				arg++;
			if (!IS_STRING(arg)) return R_NONE;
			Val_Init_String(D_OUT, Copy_Sequence(VAL_SERIES(arg)));
			return R_OUT;

		default:
			raise Error_Cannot_Reflect(VAL_TYPE(value), arg);
		}
		break;
	}

	raise Error_Illegal_Action(VAL_TYPE(value), action);
}
