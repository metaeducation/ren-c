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

enum {
	TYPE_UINT8,
	TYPE_INT8,
	TYPE_UINT16,
	TYPE_INT16,
	TYPE_UINT32,
	TYPE_INT32,
	TYPE_INT64,
	TYPE_UINT64,
	TYPE_INTEGER,

	TYPE_FLOAT,
	TYPE_DOUBLE,
	TYPE_DECIMAL,

	TYPE_POINTER,
	TYPE_STRUCT
};

#define IS_INTEGER_TYPE(t) ((t) < TYPE_INTEGER)
#define IS_DECIMAL_TYPE(t) ((t) > TYPE_INTEGER && (t) < TYPE_DECIMAL)
#define IS_NUMERIC_TYPE(t) (IS_INTEGER_TYPE(t) || IS_DECIMAL_TYPE(t))


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

static get_scalar(REBSTU *stu, struct Struct_Field *field, REBYTE *data, REBVAL *val)
{
	switch (field->type) {
		case TYPE_UINT8:
			SET_INTEGER(val, *(u8*)data);
			break;
		case TYPE_INT8:
			SET_INTEGER(val, *(i8*)data);
			break;
		case TYPE_UINT16:
			SET_INTEGER(val, *(u16*)data);
			break;
		case TYPE_INT16:
			SET_INTEGER(val, *(i8*)data);
			break;
		case TYPE_UINT32:
			SET_INTEGER(val, *(u32*)data);
			break;
		case TYPE_INT32:
			SET_INTEGER(val, *(i32*)data);
			break;
		case TYPE_UINT64:
			SET_INTEGER(val, *(u64*)data);
			break;
		case TYPE_INT64:
			SET_INTEGER(val, *(i64*)data);
			break;
		case TYPE_FLOAT:
			SET_DECIMAL(val, *(float*)data);
			break;
		case TYPE_DOUBLE:
			SET_DECIMAL(val, *(double*)data);
			break;
		case TYPE_POINTER:
			SET_INTEGER(val, (u64)*(void**)data);
			break;
		case TYPE_STRUCT:
			{
				SET_TYPE(val, REB_STRUCT);
				VAL_STRUCT_FIELDS(val) = field->fields;
				VAL_STRUCT_SPEC(val) = field->spec;
				VAL_STRUCT_DATA(val) = stu->data;
				VAL_STRUCT_OFFSET(val) = data - SERIES_DATA(VAL_STRUCT_DATA(val));
				VAL_STRUCT_LEN(val) = field->size;
			}
			break;
		default:
			/* should never be here */
			return FALSE;
	}
	return TRUE;
}

/***********************************************************************
**
*/	static REBFLG Get_Struct_Var(REBSTU *stu, REBVAL *word, REBVAL *val)
/*
***********************************************************************/
{
	struct Struct_Field *field = NULL;
	REBCNT i = 0;
	field = (struct Struct_Field *)SERIES_DATA(stu->fields);
	for (i = 0; i < SERIES_TAIL(stu->fields); i ++, field ++) {
		if (VAL_WORD_CANON(word) == VAL_SYM_CANON(BLK_SKIP(PG_Word_Table.series, field->sym))) {
			if (field->dimension > 1) {
				SET_TYPE(val, REB_BLOCK);
				REBSER *ser = Make_Block(field->dimension);
				REBCNT n = 0;
				for (n = 0; n < field->dimension; n ++) {
					REBVAL elem;
					get_scalar(stu, field, SERIES_SKIP(stu->data, stu->offset + field->offset + n * field->size), &elem);
					Append_Val(ser, &elem);
				}
				VAL_SERIES(val) = ser;
				VAL_INDEX(val) = 0;
			} else {
				get_scalar(stu, field, SERIES_SKIP(stu->data, stu->offset + field->offset), val);
			}
			return TRUE;
		}
	}
	return FALSE;
}


/***********************************************************************
**
*/	static void Set_Struct_Vars(REBSTU *strut, REBVAL *blk)
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
	u64 i = 0;
	double d = 0;
	switch (VAL_TYPE(val)) {
		case REB_DECIMAL:
			if (!IS_NUMERIC_TYPE(field->type)) {
				Trap_Type(val);
			}
			d = VAL_DECIMAL(val);
			i = (u64) d;
			break;
		case REB_INTEGER:
			if (!IS_NUMERIC_TYPE(field->type)
				|| field->type == TYPE_POINTER) {
				Trap_Type(val);
			}
			i = (u64) VAL_INT64(val);
			d = (double)i;
			break;
		case REB_STRUCT:
			if (TYPE_STRUCT != field->type) {
				Trap_Type(val);
			}
			break;
		default:
			Trap_Type(val);
	}

	switch (field->type) {
		case TYPE_INT8:
			*(i8*)data = (i8)i;
			break;
		case TYPE_UINT8:
			*(u8*)data = (u8)i;
			break;
		case TYPE_INT16:
			*(i16*)data = (i16)i;
			break;
		case TYPE_UINT16:
			*(u16*)data = (u16)i;
			break;
		case TYPE_INT32:
			*(i32*)data = (i32)i;
			break;
		case TYPE_UINT32:
			*(u32*)data = (u32)i;
			break;
		case TYPE_INT64:
			*(i64*)data = (i64)i;
			break;
		case TYPE_UINT64:
			*(u64*)data = (u64)i;
			break;
		case TYPE_POINTER:
			*(void**)data = (void*)i;
			break;
		case TYPE_FLOAT:
			*(float*)data = (float)d;
			break;
		case TYPE_DOUBLE:
			*(double*)data = (double)d;
			break;
		case TYPE_STRUCT:
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
*/	static REBFLG Set_Struct_Var(REBSTU *stu, REBVAL *word, REBVAL *elem, REBVAL *val)
/*
***********************************************************************/
{
	struct Struct_Field *field = NULL;
	REBCNT i = 0;
	field = (struct Struct_Field *)SERIES_DATA(stu->fields);
	for (i = 0; i < SERIES_TAIL(stu->fields); i ++, field ++) {
		if (VAL_WORD_CANON(word) == VAL_SYM_CANON(BLK_SKIP(PG_Word_Table.series, field->sym))) {
			if (field->dimension > 1) {
				if (elem == NULL) { //set the whole array
					REBCNT n = 0;
					if ((!IS_BLOCK(val) || field->dimension != VAL_LEN(val))) {
						return FALSE;
					}

					for(n = 0; n < field->dimension; n ++) {
						if (!assign_scalar(field, SERIES_SKIP(stu->data, field->offset + n * field->size), val)) {
							return FALSE;
						}
					}

				} else {// set only one element
					if (!IS_INTEGER(elem)
						|| VAL_INT32(elem) <= 0
						|| VAL_INT32(elem) > field->dimension) {
						return FALSE;
					}
					return assign_scalar(field,
										 SERIES_SKIP(stu->data, stu->offset + field->offset + (VAL_INT32(elem) - 1) * field->size),
										 val);
				}
				return TRUE;
			} else {
				return assign_scalar(field,
									 SERIES_SKIP(stu->data, stu->offset + field->offset),
									 val);
			}
			return TRUE;
		}
	}
	return FALSE;
}

/***********************************************************************
**
*/	REBFLG MT_Struct(REBVAL *out, REBVAL *data, REBCNT type)
/*
***********************************************************************/
{
	//RL_Print("%s\n", __func__);
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
		VAL_STRUCT_OFFSET(out) = 0;

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
						field->type = TYPE_UINT8;
						field->size = 1;
						break;
					case SYM_INT8:
						field->type = TYPE_INT8;
						field->size = 1;
						break;
					case SYM_UINT16:
						field->type = TYPE_UINT16;
						field->size = 2;
						break;
					case SYM_INT16:
						field->type = TYPE_INT16;
						field->size = 2;
						break;
					case SYM_UINT32:
						field->type = TYPE_UINT32;
						field->size = 4;
						break;
					case SYM_INT32:
						field->type = TYPE_INT32;
						field->size = 4;
						break;
					case SYM_UINT64:
						field->type = TYPE_UINT64;
						field->size = 8;
						break;
					case SYM_INT64:
						field->type = TYPE_INT64;
						field->size = 8;
						break;
					case SYM_FLOAT:
						field->type = TYPE_FLOAT;
						field->size = 4;
						break;
					case SYM_DOUBLE:
						field->type = TYPE_DOUBLE;
						field->size = 8;
						break;
					case SYM_POINTER:
						field->type = TYPE_POINTER;
						field->size = sizeof(void*);
						break;
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
							field->type = TYPE_STRUCT;
							field->fields = VAL_STRUCT_FIELDS(inner);
							field->spec = VAL_STRUCT_SPEC(inner);
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
				field->type = TYPE_STRUCT;
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
			} else if (field->type == TYPE_STRUCT && field->dimension == 1) { /* [struct-a sa] */
				memcpy(SERIES_SKIP(VAL_STRUCT_DATA(out), offset), SERIES_DATA(VAL_STRUCT_DATA(init)), field->size);
			} else {
				memset(SERIES_SKIP(VAL_STRUCT_DATA(out), offset), 0, field->size * field->dimension);
			}

			REBCNT step = field->size * field->dimension;
			if (step > VAL_STRUCT_LIMIT) {
				Trap1(RE_SIZE_LIMIT, out);
			}

			offset +=  step;
			if (offset > VAL_STRUCT_LIMIT) {
				Trap1(RE_SIZE_LIMIT, out);
			}

			++ field_idx;

			DS_POP; /* pop up the inner struct*/
		}

		VAL_STRUCT_LEN(out) = offset;

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
	struct Struct_Field *field = NULL;
	REBCNT i = 0;
	REBSTU *stu = &VAL_STRUCT(pvs->value);
	if (!IS_WORD(pvs->select)) {
		return PE_BAD_SELECT;
	}
	if (! pvs->setval || NOT_END(pvs->path + 1)) {
		if (!Get_Struct_Var(stu, pvs->select, pvs->store)) {
			return PE_BAD_SELECT;
		}

		/* Setting element to an array in the struct:
		 * struct/field/1: 0
		 * */
		if (pvs->setval
			&& IS_BLOCK(pvs->store)
			&& IS_END(pvs->path + 2)) {
			REBVAL *sel = pvs->select;
			pvs->value = pvs->store;
			Next_Path(pvs); // sets value in pvs->value
			if (!Set_Struct_Var(stu, sel, pvs->select, pvs->value)) {
				return PE_BAD_SET;
			}
			return PE_OK;
		}
		return PE_USE;
	} else {// setval && END
		if (!Set_Struct_Var(stu, pvs->select, NULL, pvs->setval)) {
			return PE_BAD_SET;
		}
		return PE_OK;
	}
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
			{
				if (!IS_BINARY(arg)) {
					Trap_Types(RE_EXPECT_VAL, REB_BINARY, VAL_TYPE(arg));
				}

				if (VAL_LEN(arg) != SERIES_TAIL(VAL_STRUCT_DATA(val))) {
					Trap_Arg(arg);
				}
				memcpy(SERIES_DATA(VAL_STRUCT_DATA(val)),
					   SERIES_DATA(VAL_SERIES(arg)),
					   SERIES_TAIL(VAL_STRUCT_DATA(val)));
			}
			break;
		case A_REFLECT:
			{
				REBINT n = What_Reflector(arg); // zero on error
				switch (n) {
					case OF_VALUES:
						SET_BINARY(ret, Copy_Series_Part(VAL_STRUCT_DATA(val), VAL_STRUCT_OFFSET(val), VAL_STRUCT_LEN(val)));
						break;
					case OF_SPEC:
						Set_Block(ret, Clone_Block(VAL_STRUCT_SPEC(val)));
						Unbind_Block(VAL_BLK(val), TRUE);
						break;
					default:
						Trap_Reflect(REB_STRUCT, arg);
				}
			}
			break;

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
