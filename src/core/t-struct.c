/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2012 REBOL Technologies
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
**  Module:  t-strut.c
**  Summary: graphical object datatype
**  Section: datatypes
**  Author:  Shixin Zeng
**  Notes:
**
***********************************************************************/

#include "sys-core.h"

const REBCNT Struct_Flag_Words[] = {
	0, 0
};

struct Stru_Field {
	REBCNT offset;
	REBCNT type; /* rebol type */
	REBCNT bytes;
	REBCNT sym;
};

/***********************************************************************
**
*/	REBINT CT_Struct(REBVAL *a, REBVAL *b, REBINT mode)
/*
***********************************************************************/
{
	if (mode >= 0)
		return VAL_STRUCT_DATA(a) == VAL_STRUCT_DATA(b);
	return -1;
}

/***********************************************************************
**
*/	static REBFLG Set_STRUCT_Var(REBSTU *strut, REBVAL *word, REBVAL *val)
/*
***********************************************************************/
{
	switch (VAL_WORD_CANON(word)) {
		default:
			return FALSE;
	}
	return TRUE;
}


/***********************************************************************
**
*/	static REBFLG Get_STRUCT_Var(REBSTU *strut, REBVAL *word, REBVAL *val)
/*
***********************************************************************/
{
	switch (VAL_WORD_CANON(word)) {

	default:
		return FALSE;
	}
	return TRUE;
}


/***********************************************************************
**
*/	static void Set_STRUCT_Vars(REBSTU *strut, REBVAL *blk)
/*
***********************************************************************/
{
}


/***********************************************************************
**
*/	REBSER *Struct_To_Block(REBSTU *strut)
/*
**		Used by MOLD to create a block.
**
***********************************************************************/
{
	REBSER *ser = Make_Block(10);
	return ser;
}


/***********************************************************************
**
*/	REBFLG MT_Struct(REBVAL *out, REBVAL *data, REBCNT type)
/*
***********************************************************************/
{
	return FALSE;
}


/***********************************************************************
**
*/	REBINT PD_Struct(REBPVS *pvs)
/*
***********************************************************************/
{
	return PE_BAD_SELECT;
}


/***********************************************************************
**
*/	REBTYPE(Struct)
/*
***********************************************************************/
{
	REBVAL *val;
	REBVAL *arg;
	REBSTU *strut;
	REBSTU *nstrut;
	REBCNT index;
	REBCNT tail;
	REBCNT len;

	arg = D_ARG(2);
	val = D_RET;
	*val = *D_ARG(1);
	strut = 0;

	// unary actions
	switch(action) {
		case A_MAKE:
		case A_TO:
			val = D_ARG(1);

			REBVAL *ret = DS_RETURN;
			// Clone an existing STRUCT:
			if (IS_STRUCT(val)) {
				*ret = *val;
				VAL_STRUCT_SPEC(ret) = Copy_Series(VAL_STRUCT_SPEC(val));
				VAL_STRUCT_DATA(ret) = Copy_Series(VAL_STRUCT_DATA(val));
				VAL_STRUCT_FIELDS(ret) = Copy_Series(VAL_STRUCT_FIELDS(val));
			} else if (!IS_DATATYPE(val)) {
				goto is_arg_error;
			} else {
				// Initialize STRUCT from block:
				if (IS_BLOCK(arg)) {
					MT_Struct(ret, VAL_BLK_DATA(arg), REB_STRUCT);
				} else {
					Trap_Make(REB_STRUCT, arg);
				}
			}
			SET_TYPE(ret, REB_STRUCT);
			break;

		case A_CHANGE:

		case A_LENGTHQ:
		default:
			Trap_Action(REB_STRUCT, action);
	}
	return R_RET;

is_arg_error:
	Trap_Types(RE_EXPECT_VAL, REB_STRUCT, VAL_TYPE(arg));

is_false:
	return R_FALSE;

is_true:
	return R_TRUE;
}
