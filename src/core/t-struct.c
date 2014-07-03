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

#include <stdlib.h>
#include "sys-core.h"

const REBCNT Struct_Flag_Words[] = {
	0, 0
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

static REBOOL assign_scalar(struct Struct_Field *field, REBYTE *data, REBVAL *val)
{
	switch (field->type) {
		case REB_ISSUE: /* pointer */
			if (!IS_INTEGER(val)) {
				Trap_Types(RE_EXPECT_VAL, REB_INTEGER, VAL_TYPE(val));
			}
			*(void**)data = (void*)(VAL_INT64(val));
			break;
		case REB_INTEGER:
			{
				u64 v = 0;
				switch (VAL_TYPE(val)) {
					case REB_DECIMAL:
						v = (u64) VAL_DECIMAL(val);
						break;
					case REB_INTEGER:
						v = (u64) VAL_INT64(val);
						break;
					default:
						Trap_Type(val);
				}
				switch (field->size) {
					case 1:
						*(u8*)data = (u8)v;
						break;
					case 2:
						*(u16*)data = (u16)v;
						break;
					case 4:
						*(u32*)data = (u32)v;
						break;
					case 8:
						*(u64*)data = v;
						break;
					default:
						/* should never be here */
						return FALSE;
				}
			}
			break;
		case REB_DECIMAL:
			{
				double v = 0;
				switch (VAL_TYPE(val)) {
					case REB_DECIMAL:
						v = (double)VAL_DECIMAL(val);
						break;
					case REB_INTEGER:
						v = (double)VAL_INT64(val);
						break;
					default:
						Trap_Type(val);
				}
				switch (field->size) {
					case 4:
						*(float*)data = (float)v;
						break;
					case 8:
						*(double*)data = v;
						break;
					default:
						/* should never be here */
						return FALSE;
				}
			}
			break;
		case REB_STRUCT:
			if (!IS_STRUCT(val)) {
				Trap_Types(RE_EXPECT_VAL, REB_STRUCT, VAL_TYPE(val));
			}
			memcpy(data, VAL_STRUCT_DATA(val), field->size);
			break;
		default:
			/* should never be here */
			return FALSE;
	}
	return TRUE;
}

/***********************************************************************
**
*/	REBFLG MT_Struct(REBVAL *out, REBVAL *data, REBCNT type)
/*
***********************************************************************/
{
	RL_Print("%s\n", __func__);
	REBINT max_fields = 16;
	VAL_STRUCT_FIELDS(out) = Make_Series(max_fields, sizeof(struct Struct_Field), FALSE);
	BARE_SERIES(VAL_STRUCT_FIELDS(out));
	if (IS_BLOCK(data)) {
		//Reduce_Block_No_Set(VAL_SERIES(data), 0, NULL);
		//data = DS_POP;
		REBVAL *blk = VAL_BLK_DATA(data);
		REBINT field_idx = 0; /* for field index */
		REBCNT offset = 0; /* offset in data */
		REBCNT eval_idx = 0; /* for spec block evaluation */
		REBVAL *init = NULL; /* for result to save in data */
		REBOOL expect_init = FALSE;

		VAL_STRUCT_SPEC(out) = Copy_Series(VAL_SERIES(data));
		VAL_STRUCT_DATA(out) = Make_Series(max_fields << 2, 1, FALSE);
		BARE_SERIES(VAL_STRUCT_DATA(out));

		/* set type early such that GC will handle it correctly, i.e, not collect series in the struct */
		SET_TYPE(out, REB_STRUCT);

		while (NOT_END(blk)) {
			EXPAND_SERIES_TAIL(VAL_STRUCT_FIELDS(out), 1);
			REBVAL *inner;

			DS_PUSH_NONE;
			inner = DS_TOP; /* save in stack so that it won't be GC'ed when MT_Struct is recursively called */

			struct Struct_Field *field = (struct Struct_Field *)SERIES_SKIP(VAL_STRUCT_FIELDS(out), field_idx);
			field->offset = offset;
			if (IS_WORD(blk)) {
				switch (VAL_WORD_CANON(blk)) {
					case SYM_UINT8:
					case SYM_INT8:
						field->type = REB_INTEGER;
						field->size = 1;
						break;
					case SYM_UINT16:
					case SYM_INT16:
						field->type = REB_INTEGER;
						field->size = 2;
						break;
					case SYM_UINT32:
					case SYM_INT32:
						field->type = REB_INTEGER;
						field->size = 4;
						break;
					case SYM_UINT64:
					case SYM_INT64:
						field->type = REB_INTEGER;
						field->size = 8;
						break;
					case SYM_FLOAT:
						field->type = REB_DECIMAL;
						field->size = 4;
						break;
					case SYM_DOUBLE:
						field->type = REB_DECIMAL;
						field->size = 8;
						break;
					case SYM_POINTER:
						field->type = REB_INTEGER;
						field->size = sizeof(void*);
						break;
						/* //These types are confusing
					case SYM_INTEGER_TYPE:
						field->type = REB_INTEGER;
						field->size = 4;
						break;
					case SYM_DECIMAL_TYPE:
						field->type = REB_DECIMAL;
						field->size = 8;
						break;
						*/
					case SYM_STRUCT_TYPE:
						++ blk;
						if (IS_BLOCK(blk)) {
							REBINT res;

							/*
							   SAVE_SERIES(VAL_STRUCT_DATA(out));
							   SAVE_SERIES(VAL_STRUCT_SPEC(out));
							   SAVE_SERIES(VAL_STRUCT_FIELDS(out));
							   */

							res = MT_Struct(inner, blk, REB_STRUCT);

							/*
							   UNSAVE_SERIES(VAL_STRUCT_FIELDS(out));
							   UNSAVE_SERIES(VAL_STRUCT_SPEC(out));
							   UNSAVE_SERIES(VAL_STRUCT_DATA(out));
							   */

							if (!res) {
								RL_Print("Failed to make nested struct!\n");
								return FALSE;
							}

							field->size = SERIES_TAIL(VAL_STRUCT_DATA(inner));
							field->type = REB_STRUCT;
							field->fields = VAL_STRUCT_FIELDS(inner);
							init = inner; /* a shortcut for struct intialization */
						} else {
							Trap_Types(RE_EXPECT_VAL, REB_BLOCK, VAL_TYPE(blk));
						}
						break;
					default:
						Trap_Type(blk);
				}
			} else if (IS_STRUCT(blk)) { //[struct-a b: val-a] 
				field->size = SERIES_TAIL(VAL_STRUCT_DATA(blk));
				field->type = REB_STRUCT;
				init = blk; /* a shortcut for struct intialization */
			} else {
				Trap_Type(blk);
			}
			++ blk;

			if (IS_BLOCK(blk)) {// make struct! [integer! [2] a: [0 0]]
				/*
				SAVE_SERIES(VAL_STRUCT_DATA(out));
				SAVE_SERIES(VAL_STRUCT_SPEC(out));
				SAVE_SERIES(VAL_STRUCT_FIELDS(out));
				*/

				REBVAL *ret = Do_Blk(VAL_SERIES(blk), 0);

				/*
				UNSAVE_SERIES(VAL_STRUCT_FIELDS(out));
				UNSAVE_SERIES(VAL_STRUCT_SPEC(out));
				UNSAVE_SERIES(VAL_STRUCT_DATA(out));
				*/

				if (!IS_INTEGER(ret)) {
					Trap_Types(RE_EXPECT_VAL, REB_INTEGER, VAL_TYPE(blk));
				}
				field->dimension = (REBCNT)VAL_INT64(ret);
				++ blk;
			} else {
				field->dimension = 1; /* scalar */
			}

			if (IS_SET_WORD(blk)) {
				field->sym = VAL_WORD_SYM(blk); 
				++ blk;
				expect_init = TRUE;
			} else if (IS_WORD(blk)) {
				field->sym = VAL_WORD_SYM(blk); 
				++ blk;
				expect_init = FALSE;
			} else {
				Trap_Type(blk);
			}

			EXPAND_SERIES_TAIL(VAL_STRUCT_DATA(out), field->size * field->dimension);

			if (expect_init) {
				eval_idx = blk - VAL_BLK_DATA(data);

				/*
				SAVE_SERIES(VAL_STRUCT_DATA(out));
				SAVE_SERIES(VAL_STRUCT_SPEC(out));
				SAVE_SERIES(VAL_STRUCT_FIELDS(out));
				*/

				eval_idx = Do_Next(VAL_SERIES(data), eval_idx, 0);

				/*
				UNSAVE_SERIES(VAL_STRUCT_FIELDS(out));
				UNSAVE_SERIES(VAL_STRUCT_SPEC(out));
				UNSAVE_SERIES(VAL_STRUCT_DATA(out));
				*/

				blk = VAL_BLK_SKIP(data, eval_idx);
				init = DS_POP; //Do_Next saves result on stack

				if (field->dimension > 1) {
					if (IS_BLOCK(init)) {
						if (VAL_LEN(init) != field->dimension) {
							Trap1(RE_INVALID_DATA, init);
						}
						/* assign */
						REBVAL *elem = VAL_BLK_DATA(init);
						REBCNT elem_offset = 0;
						while (NOT_END(elem)) {
							if (!assign_scalar(field, SERIES_SKIP(VAL_STRUCT_DATA(out), offset + elem_offset), elem)) {
								RL_Print("Failed to assign element value\n");
								goto failed;
							}
							elem_offset += field->size;
							elem ++;
						}
					} else {
						Trap_Types(RE_EXPECT_VAL, REB_BLOCK, VAL_TYPE(blk));
					}
				} else {
					/* scalar */
					if (!assign_scalar(field, SERIES_SKIP(VAL_STRUCT_DATA(out), offset), init)) {
						RL_Print("Failed to assign scalar value\n");
						goto failed;
					}
				}
			} else if (field->type == REB_STRUCT && field->dimension == 1) { /* [struct-a sa] */
				memcpy(SERIES_SKIP(VAL_STRUCT_DATA(out), offset), SERIES_DATA(VAL_STRUCT_DATA(init)), field->size);
			} else {
				memset(SERIES_SKIP(VAL_STRUCT_DATA(out), offset), 0, field->size * field->dimension);
			}

			offset += field->size * field->dimension;

			++ field_idx;

			DS_POP; /* pop up the inner struct*/
		}
		return TRUE;
	}

failed:
	Free_Series(VAL_STRUCT_FIELDS(out));
	Free_Series(VAL_STRUCT_SPEC(out));
	Free_Series(VAL_STRUCT_DATA(out));

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
	val = D_ARG(1);
	strut = 0;

	REBVAL *ret = DS_RETURN;
	// unary actions
	switch(action) {
		case A_MAKE:
			//RL_Print("%s, %d, Make struct action\n", __func__, __LINE__);
		case A_TO:
			//RL_Print("%s, %d, To struct action\n", __func__, __LINE__);

			// Clone an existing STRUCT:
			if (IS_STRUCT(val)) {
				*ret = *val;
				/* Read only fields */
				VAL_STRUCT_SPEC(ret) = VAL_STRUCT_SPEC(val);
				VAL_STRUCT_FIELDS(ret) = VAL_STRUCT_FIELDS(val);

				/* writable field */
				VAL_STRUCT_DATA(ret) = Copy_Series(VAL_STRUCT_DATA(val));
			} else if (!IS_DATATYPE(val)) {
				goto is_arg_error;
			} else {
				// Initialize STRUCT from block:
				// make struct! [float a: 0]
				// make struct! [double a: 0]
				if (IS_BLOCK(arg)) {
					if (!MT_Struct(ret, arg, REB_STRUCT)) {
						goto is_arg_error;
					}
				} else {
					Trap_Make(REB_STRUCT, arg);
				}
			}
			SET_TYPE(ret, REB_STRUCT);
			break;

		case A_CHANGE:

		case A_LENGTHQ:
			SET_INTEGER(ret, SERIES_TAIL(VAL_STRUCT_DATA(val)));
			break;
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
