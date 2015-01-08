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
**  Module:  t-routine.c
**  Summary: External Routine Support
**  Section: datatypes
**  Author:  Shixin Zeng
**  Notes:
**
***********************************************************************/

#include <stdio.h>
#include "sys-core.h"

#include <ffi.h>

#define QUEUE_EXTRA_MEM(v, p) do {\
	*(void**) SERIES_SKIP(v->extra_mem, SERIES_TAIL(v->extra_mem)) = p;\
	EXPAND_SERIES_TAIL(v->extra_mem, 1);\
} while (0)

static ffi_type * struct_type_to_ffi [STRUCT_TYPE_MAX];

static void process_type_block(REBVAL *out, REBVAL *blk, REBCNT n);

static void init_type_map()
{
	if (struct_type_to_ffi[0]) return;
	struct_type_to_ffi[STRUCT_TYPE_UINT8] = &ffi_type_uint8;
	struct_type_to_ffi[STRUCT_TYPE_INT8] = &ffi_type_sint8;
	struct_type_to_ffi[STRUCT_TYPE_UINT16] = &ffi_type_uint16;
	struct_type_to_ffi[STRUCT_TYPE_INT16] = &ffi_type_sint16;
	struct_type_to_ffi[STRUCT_TYPE_UINT32] = &ffi_type_uint32;
	struct_type_to_ffi[STRUCT_TYPE_INT32] = &ffi_type_sint32;
	struct_type_to_ffi[STRUCT_TYPE_UINT64] = &ffi_type_uint64;
	struct_type_to_ffi[STRUCT_TYPE_INT64] = &ffi_type_sint64;

	struct_type_to_ffi[STRUCT_TYPE_FLOAT] = &ffi_type_float;
	struct_type_to_ffi[STRUCT_TYPE_DOUBLE] = &ffi_type_double;

	struct_type_to_ffi[STRUCT_TYPE_POINTER] = &ffi_type_pointer;
}

/***********************************************************************
**
*/	REBINT CT_Routine(REBVAL *a, REBVAL *b, REBINT mode)
/*
***********************************************************************/
{
	//RL_Print("%s, %d\n", __func__, __LINE__);
	if (mode >= 0) {
		return VAL_ROUTINE_INFO(a) == VAL_ROUTINE_INFO(b);
	}
	return -1;
}

/***********************************************************************
**
*/	REBINT CT_Callback(REBVAL *a, REBVAL *b, REBINT mode)
/*
***********************************************************************/
{
	//RL_Print("%s, %d\n", __func__, __LINE__);
	return -1;
}

static REBCNT n_struct_fields (REBSER *fields)
{
	REBCNT n_fields = 0;
	REBCNT i = 0;
	for (i = 0; i < SERIES_TAIL(fields); i ++) {
		struct Struct_Field *field = (struct Struct_Field*)SERIES_SKIP(fields, i);
		if (field->type != STRUCT_TYPE_STRUCT) {
			n_fields += field->dimension;
		} else {
			n_fields += n_struct_fields(field->fields);
		}
	}
	return n_fields;
}

static ffi_type* struct_to_ffi(REBVAL *out, REBSER *fields)
{
	ffi_type *args = (ffi_type*) SERIES_DATA(VAL_ROUTINE_FFI_ARGS(out));
	REBCNT i = 0, j = 0;
	REBCNT n_basic_type = 0;

	ffi_type *stype = OS_MAKE(sizeof(ffi_type));
	//printf("allocated stype at: %p\n", stype);
	QUEUE_EXTRA_MEM(VAL_ROUTINE_INFO(out), stype);

	stype->size = stype->alignment = 0;
	stype->type = FFI_TYPE_STRUCT;

	stype->elements = OS_MAKE(sizeof(ffi_type *) * (1 + n_struct_fields(fields))); /* one extra for NULL */
	//printf("allocated stype elements at: %p\n", stype->elements);
	QUEUE_EXTRA_MEM(VAL_ROUTINE_INFO(out), stype->elements);

	for (i = 0; i < SERIES_TAIL(fields); i ++) {
		struct Struct_Field *field = (struct Struct_Field*)SERIES_SKIP(fields, i);
		if (field->type != STRUCT_TYPE_STRUCT) {
			if (struct_type_to_ffi[field->type]) {
				REBCNT n = 0;
				for (n = 0; n < field->dimension; n ++) {
					stype->elements[j++] = struct_type_to_ffi[field->type];
				}
			} else {
				return NULL;
			}
		} else {
			ffi_type *subtype = struct_to_ffi(out, field->fields);
			if (subtype) {
				REBCNT n = 0;
				for (n = 0; n < field->dimension; n ++) {
					stype->elements[j++] = subtype;
				}
			} else {
				return NULL;
			}
		}
	}
	stype->elements[j] = NULL;

	return stype;
}

/* convert the type of "elem", and store it in "out" with index of "idx"
 */
static REBOOL rebol_type_to_ffi(REBVAL *out, REBVAL *elem, REBCNT idx)
{
	ffi_type **args = (ffi_type**) SERIES_DATA(VAL_ROUTINE_FFI_ARGS(out));
	REBVAL *rebol_args = NULL;
	REBVAL *arg_structs = (REBVAL*)SERIES_DATA(VAL_ROUTINE_FFI_ARG_STRUCTS(out));
	if (idx) {
		// when it's first call for return type, all_args has not been initialized yet
		if (ROUTINE_GET_FLAG(VAL_ROUTINE_INFO(out), ROUTINE_VARARGS)
			&& idx > SERIES_TAIL(VAL_ROUTINE_FIXED_ARGS(out))) {
			rebol_args = (REBVAL*)SERIES_DATA(VAL_ROUTINE_ALL_ARGS(out));
		} else {
			rebol_args = (REBVAL*)SERIES_DATA(VAL_ROUTINE_ARGS(out));
		}
	}

	if (IS_WORD(elem)) {
		switch (VAL_WORD_CANON(elem)) {
			case SYM_VOID:
				args[idx] = &ffi_type_void;
				break;
			case SYM_UINT8:
				args[idx] = &ffi_type_uint8;
				if (idx) TYPE_SET(&rebol_args[idx], REB_INTEGER);
				break;
			case SYM_INT8:
				args[idx] = &ffi_type_sint8;
				if (idx) TYPE_SET(&rebol_args[idx], REB_INTEGER);
				break;
			case SYM_UINT16:
				args[idx] = &ffi_type_uint16;
				if (idx) TYPE_SET(&rebol_args[idx], REB_INTEGER);
				break;
			case SYM_INT16:
				args[idx] = &ffi_type_sint16;
				if (idx) TYPE_SET(&rebol_args[idx], REB_INTEGER);
				break;
			case SYM_UINT32:
				args[idx] = &ffi_type_uint32;
				if (idx) TYPE_SET(&rebol_args[idx], REB_INTEGER);
				break;
			case SYM_INT32:
				args[idx] = &ffi_type_sint32;
				if (idx) TYPE_SET(&rebol_args[idx], REB_INTEGER);
				break;
			case SYM_UINT64:
				args[idx] = &ffi_type_uint64;
				if (idx) TYPE_SET(&rebol_args[idx], REB_INTEGER);
				break;
			case SYM_INT64:
				args[idx] = &ffi_type_sint64;
				if (idx) TYPE_SET(&rebol_args[idx], REB_INTEGER);
				break;
			case SYM_FLOAT:
				args[idx] = &ffi_type_float;
				if (idx) TYPE_SET(&rebol_args[idx], REB_DECIMAL);
				break;
			case SYM_DOUBLE:
				args[idx] = &ffi_type_double;
				if (idx) TYPE_SET(&rebol_args[idx], REB_DECIMAL);
				break;
			case SYM_POINTER:
				args[idx] = &ffi_type_pointer;
				if (idx) {
					TYPE_SET(&rebol_args[idx], REB_INTEGER);
					TYPE_SET(&rebol_args[idx], REB_STRING);
					TYPE_SET(&rebol_args[idx], REB_BINARY);
					TYPE_SET(&rebol_args[idx], REB_VECTOR);
				}
				break;
			default:
				return FALSE;
		}
		Append_Value(VAL_ROUTINE_FFI_ARG_STRUCTS(out)); /* fill with none */
	} else if (IS_STRUCT(elem)) {
		ffi_type *ftype = struct_to_ffi(out, VAL_STRUCT_FIELDS(elem));
		REBVAL *to = NULL;
		if (ftype) {
			args[idx] = ftype;
			if (idx) {
				TYPE_SET(&rebol_args[idx], REB_STRUCT);
			}
		} else {
			return FALSE;
		}
		if (idx == 0) {
			to = BLK_HEAD(VAL_ROUTINE_FFI_ARG_STRUCTS(out));
		} else {
			to = Append_Value(VAL_ROUTINE_FFI_ARG_STRUCTS(out));
		}
		Copy_Struct_Val(elem, to); //for callback and return value
	} else {
		return FALSE;
	}
	return TRUE;
}

/* make a copy of the argument 
 * arg referes to return value when idx = 0
 * function args start from idx = 1
 *
 * For FFI_TYPE_POINTER, a temperary pointer could be needed 
 * (whose address is returned). The pointer is stored in rebol 
 * stack, so DS_POP is needed after the function call is done.
 * The number to pop is returned by pop
 * */
static void *arg_to_ffi(REBVAL *rot, REBVAL *arg, REBCNT idx, REBINT *pop)
{
	ffi_type **args = (ffi_type**)SERIES_DATA(VAL_ROUTINE_FFI_ARGS(rot));
	REBSER *rebol_args = NULL;

	if (ROUTINE_GET_FLAG(VAL_ROUTINE_INFO(rot), ROUTINE_VARARGS)) {
		rebol_args = VAL_ROUTINE_ALL_ARGS(rot);
	} else {
		rebol_args = VAL_ROUTINE_ARGS(rot);
	}
	switch (args[idx]->type) {
		case FFI_TYPE_UINT8:
			if (!IS_INTEGER(arg)) {
				Trap3(RE_EXPECT_ARG, DSF_WORD(DSF), BLK_SKIP(rebol_args, idx), arg);
			} else {
#ifdef BIG_ENDIAN
				u8 i = (u8) VAL_INT64(arg);
				memcpy(&VAL_INT64(arg), &i, sizeof(u8));
#endif
				return &VAL_INT64(arg);
			}
		case FFI_TYPE_SINT8:
			if (!IS_INTEGER(arg)) {
				Trap3(RE_EXPECT_ARG, DSF_WORD(DSF), BLK_SKIP(rebol_args, idx), arg);
			} else {
#ifdef BIG_ENDIAN
				i8 i = (i8) VAL_INT64(arg);
				memcpy(&VAL_INT64(arg), &i, sizeof(i8));
#endif
				return &VAL_INT64(arg);
			}
		case FFI_TYPE_UINT16:
			if (!IS_INTEGER(arg)) {
				Trap3(RE_EXPECT_ARG, DSF_WORD(DSF), BLK_SKIP(rebol_args, idx), arg);
			} else {
#ifdef BIG_ENDIAN
				u16 i = (u16) VAL_INT64(arg);
				memcpy(&VAL_INT64(arg), &i, sizeof(u16));
#endif
				return &VAL_INT64(arg);
			}
		case FFI_TYPE_SINT16:
			if (!IS_INTEGER(arg)) {
				Trap3(RE_EXPECT_ARG, DSF_WORD(DSF), BLK_SKIP(rebol_args, idx), arg);
			} else {
#ifdef BIG_ENDIAN
				i16 i = (i16) VAL_INT64(arg);
				memcpy(&VAL_INT64(arg), &i, sizeof(i16));
#endif
				return &VAL_INT64(arg);
			}
		case FFI_TYPE_UINT32:
			if (!IS_INTEGER(arg)) {
				Trap3(RE_EXPECT_ARG, DSF_WORD(DSF), BLK_SKIP(rebol_args, idx), arg);
			} else {
#ifdef BIG_ENDIAN
				u32 i = (u32) VAL_INT64(arg);
				memcpy(&VAL_INT64(arg), &i, sizeof(u32));
#endif
				return &VAL_INT64(arg);
			}
		case FFI_TYPE_SINT32:
			if (!IS_INTEGER(arg)) {
				Trap3(RE_EXPECT_ARG, DSF_WORD(DSF), BLK_SKIP(rebol_args, idx), arg);
			} else {
#ifdef BIG_ENDIAN
				i32 i = (i32) VAL_INT64(arg);
				memcpy(&VAL_INT64(arg), &i, sizeof(i32));
#endif
				return &VAL_INT64(arg);
			}
		case FFI_TYPE_UINT64:
		case FFI_TYPE_SINT64:
			if (!IS_INTEGER(arg)) {
				Trap3(RE_EXPECT_ARG, DSF_WORD(DSF), BLK_SKIP(rebol_args, idx), arg);
			}
			return &VAL_INT64(arg);
		case FFI_TYPE_POINTER:
			switch (VAL_TYPE(arg)) {
				case REB_INTEGER:
					return &VAL_INT64(arg);
				case REB_STRING:
				case REB_BINARY:
				case REB_VECTOR:
					{
						DS_PUSH_INTEGER((REBUPT)VAL_DATA(arg));
						(*pop) ++;
						return &VAL_INT64(DS_TOP);
					}
				default:
					Trap3(RE_EXPECT_ARG, DSF_WORD(DSF), BLK_SKIP(rebol_args, idx), arg);
			}
		case FFI_TYPE_FLOAT:
			/* hackish, store the signle precision floating point number in a double precision variable */
			if (!IS_DECIMAL(arg)) {
				Trap3(RE_EXPECT_ARG, DSF_WORD(DSF), BLK_SKIP(rebol_args, idx), arg);
			} else {
				float a = (float)VAL_DECIMAL(arg);
				memcpy(&VAL_DECIMAL(arg), &a, sizeof(a));
				return &VAL_DECIMAL(arg);
			}
		case FFI_TYPE_DOUBLE:
			if (!IS_DECIMAL(arg)) {
				Trap3(RE_EXPECT_ARG, DSF_WORD(DSF), BLK_SKIP(rebol_args, idx), arg);
			}
			return &VAL_DECIMAL(arg);
		case FFI_TYPE_STRUCT:
			/* make a copy of old binary data, such that the original one won't be modified */
			if (idx == 0) {/* returning a struct */
				Copy_Struct(&VAL_ROUTINE_RVALUE(rot), &VAL_STRUCT(arg));
			} else {
				if (IS_STRUCT(arg)) {
					VAL_STRUCT_DATA_BIN(arg) = Copy_Series(VAL_STRUCT_DATA_BIN(arg));
				} else {
					Trap3(RE_EXPECT_ARG, DSF_WORD(DSF), BLK_SKIP(rebol_args, idx), arg);
				}
			}
			return SERIES_SKIP(VAL_STRUCT_DATA_BIN(arg), VAL_STRUCT_OFFSET(arg));
		case FFI_TYPE_VOID:
			if (!idx) {
				return NULL;
			} else {
				Trap3(RE_EXPECT_ARG, DSF_WORD(DSF), BLK_SKIP(rebol_args, idx), arg);
			}
		default:
			Trap_Arg(arg);
	}
	return NULL;
}

static void prep_rvalue(REBRIN *rin,
						REBVAL *val)
{
	ffi_type * rtype = *(ffi_type**) SERIES_DATA(rin->args);
	switch (rtype->type) {
		case FFI_TYPE_UINT8:
		case FFI_TYPE_SINT8:
		case FFI_TYPE_UINT16:
		case FFI_TYPE_SINT16:
		case FFI_TYPE_UINT32:
		case FFI_TYPE_SINT32:
		case FFI_TYPE_UINT64:
		case FFI_TYPE_SINT64:
		case FFI_TYPE_POINTER:
			SET_INTEGER(val, 0);
			break;
		case FFI_TYPE_FLOAT:
		case FFI_TYPE_DOUBLE:
			SET_DECIMAL(val, 0);
			break;
		case FFI_TYPE_STRUCT:
			SET_TYPE(val, REB_STRUCT);
			break;
		case FFI_TYPE_VOID:
			break;
		default:
			Trap_Arg(val);
	}
}

/* convert the return value to rebol
 */
static void ffi_to_rebol(REBRIN *rin,
						 ffi_type *ffi_rtype,
						 void *ffi_rvalue,
						 REBVAL *rebol_ret)
{
	switch (ffi_rtype->type) {
		case FFI_TYPE_UINT8:
			SET_INTEGER(rebol_ret, *(u8*)ffi_rvalue);
			break;
		case FFI_TYPE_SINT8:
			SET_INTEGER(rebol_ret, *(i8*)ffi_rvalue);
			break;
		case FFI_TYPE_UINT16:
			SET_INTEGER(rebol_ret, *(u16*)ffi_rvalue);
			break;
		case FFI_TYPE_SINT16:
			SET_INTEGER(rebol_ret, *(i16*)ffi_rvalue);
			break;
		case FFI_TYPE_UINT32:
			SET_INTEGER(rebol_ret, *(u32*)ffi_rvalue);
			break;
		case FFI_TYPE_SINT32:
			SET_INTEGER(rebol_ret, *(i32*)ffi_rvalue);
			break;
		case FFI_TYPE_UINT64:
			SET_INTEGER(rebol_ret, *(u64*)ffi_rvalue);
			break;
		case FFI_TYPE_SINT64:
			SET_INTEGER(rebol_ret, *(i64*)ffi_rvalue);
			break;
		case FFI_TYPE_POINTER:
			SET_INTEGER(rebol_ret, (REBUPT)*(void**)ffi_rvalue);
			break;
		case FFI_TYPE_FLOAT:
			SET_DECIMAL(rebol_ret, *(float*)ffi_rvalue);
			break;
		case FFI_TYPE_DOUBLE:
			SET_DECIMAL(rebol_ret, *(double*)ffi_rvalue);
			break;
		case FFI_TYPE_STRUCT:
			Copy_Struct_Val(&RIN_RVALUE(rin), rebol_ret);
			memcpy(SERIES_SKIP(VAL_STRUCT_DATA_BIN(rebol_ret), VAL_STRUCT_OFFSET(rebol_ret)),
				   ffi_rvalue,
				   VAL_STRUCT_LEN(rebol_ret));

			break;
		case FFI_TYPE_VOID:
			break;
		default:
			Trap_Arg(rebol_ret);
	}
}

/***********************************************************************
**
*/	void Call_Routine(REBVAL *rot, REBSER *args, REBVAL *ret)
/*
***********************************************************************/
{
	REBCNT i = 0;
	void *rvalue = NULL;
	REBSER *ser = NULL;
	void ** ffi_args = NULL;
	REBINT pop = 1; /* for tmp */
	REBVAL *tmp = NULL;
	REBVAL *varargs = NULL;
	REBINT n_fixed = 0; /* nunmber of fixed arguments */

	if (VAL_ROUTINE_LIB(rot) != NULL //lib is NULL when routine is constructed from address directly
		&& IS_CLOSED_LIB(VAL_ROUTINE_LIB(rot))) {
		Trap0(RE_BAD_LIBRARY);
	}

	if (ROUTINE_GET_FLAG(VAL_ROUTINE_INFO(rot), ROUTINE_VARARGS)) {
		varargs = BLK_HEAD(args);
		if (!IS_BLOCK(varargs)) {
			Trap_Arg(varargs);
		}
		n_fixed = SERIES_TAIL(VAL_ROUTINE_FIXED_ARGS(rot)) - 1; /* first arg is 'self */
		if ((VAL_LEN(varargs) - n_fixed) % 2) {
			Trap_Arg(varargs);
		}
		ser = Make_Series(n_fixed + (VAL_LEN(varargs) - n_fixed) / 2, sizeof(void *), FALSE);
	} else if ((SERIES_TAIL(VAL_ROUTINE_FFI_ARGS(rot))) > 1) {
		ser = Make_Series(SERIES_TAIL(VAL_ROUTINE_FFI_ARGS(rot)) - 1, sizeof(void *), FALSE);
	}

	/* save ser on stack such that it won't be GC'ed */
	if (ser != NULL) {
		DS_PUSH_NONE;
		tmp = DS_TOP;
		SET_TYPE(tmp, REB_BLOCK);
		VAL_SERIES(tmp) = ser;
		ffi_args = (void **) SERIES_DATA(ser);
	}

	if (ROUTINE_GET_FLAG(VAL_ROUTINE_INFO(rot), ROUTINE_VARARGS)) {
		REBINT j = 1;
		ffi_type **args = NULL;

		VAL_ROUTINE_ALL_ARGS(rot) = Copy_Series(VAL_ROUTINE_FIXED_ARGS(rot));

		for (i = 1, j = 1; i < SERIES_TAIL(VAL_SERIES(varargs)) + 1; i ++, j ++) {
			REBVAL *reb_arg = VAL_BLK_SKIP(varargs, i - 1);
			if (i <= n_fixed) { /* fix arguments */
				if (!TYPE_CHECK(BLK_SKIP(VAL_ROUTINE_FIXED_ARGS(rot), i), VAL_TYPE(reb_arg))) {
					Trap3(RE_EXPECT_ARG, DSF_WORD(DSF), BLK_SKIP(VAL_ROUTINE_FIXED_ARGS(rot), i), reb_arg);
				}
			} else {
				/* initialize rin->args */
				REBVAL *reb_type = NULL;
				REBVAL *v = NULL;
				if (i == SERIES_TAIL(VAL_SERIES(varargs))) { /* type is missing */
					Trap_Arg(reb_arg);
				}
				reb_type = VAL_BLK_SKIP(varargs, i);
				if (!IS_BLOCK(reb_type)) {
					Trap_Arg(reb_type);
				}
				v = Append_Value(VAL_ROUTINE_ALL_ARGS(rot));
				Init_Word(v, SYM_ELLIPSIS); //FIXME, be clear
				process_type_block(rot, reb_type, j);
				i ++;
			}
			ffi_args[j - 1] = arg_to_ffi(rot, reb_arg, j, &pop);
		}
		if (VAL_ROUTINE_CIF(rot) == NULL) {
			VAL_ROUTINE_CIF(rot) = OS_MAKE(sizeof(ffi_cif));
			QUEUE_EXTRA_MEM(VAL_ROUTINE_INFO(rot), VAL_ROUTINE_CIF(rot));
		}

		/* series data could have moved */
		args = (ffi_type**)SERIES_DATA(VAL_ROUTINE_FFI_ARGS(rot));
		if (FFI_OK != ffi_prep_cif_var((ffi_cif*)VAL_ROUTINE_CIF(rot),
				VAL_ROUTINE_ABI(rot),
				n_fixed, /* number of fixed arguments */
				j - 1, /* number of all arguments */
				args[0], /* return type */
				&args[1])) {
			//RL_Print("Couldn't prep CIF_VAR\n");
			Trap_Arg(varargs);
		}
	} else {
		for (i = 1; i < SERIES_TAIL(VAL_ROUTINE_FFI_ARGS(rot)); i ++) {
			ffi_args[i - 1] = arg_to_ffi(rot, BLK_SKIP(args, i - 1), i, &pop);
		}
	}
	prep_rvalue(VAL_ROUTINE_INFO(rot), ret);
	rvalue = arg_to_ffi(rot, ret, 0, &pop);
	ffi_call(VAL_ROUTINE_CIF(rot),
			 (void (*) (void))VAL_ROUTINE_FUNCPTR(rot),
			 rvalue,
			 ffi_args);
	ffi_to_rebol(VAL_ROUTINE_INFO(rot), ((ffi_type**)SERIES_DATA(VAL_ROUTINE_FFI_ARGS(rot)))[0], rvalue, ret);
	DSP -= pop;
}

/***********************************************************************
**
*/	void Free_Routine(REBRIN *rin)
/*
***********************************************************************/
{
	REBCNT n = 0;
	for (n = 0; n < SERIES_TAIL(rin->extra_mem); n ++) {
		void *addr = *(void **)SERIES_SKIP(rin->extra_mem, n);
		//printf("freeing %p\n", addr);
		OS_FREE(addr);
	}

	UNMARK_ROUTINE(rin);
	if (IS_CALLBACK_ROUTINE(rin)) {
		ffi_closure_free(RIN_CLOSURE(rin));
	}
	Free_Node(RIN_POOL, (REBNOD*)rin);
}

static void process_type_block(REBVAL *out, REBVAL *blk, REBCNT n)
{
	if (IS_BLOCK(blk)) {
		REBVAL *t = VAL_BLK_DATA(blk);
		if (IS_WORD(t)
			&& VAL_WORD_CANON(t) == SYM_STRUCT_TYPE) {
			/* followed by struct definition */
			++ t;
			if (!IS_BLOCK(t) || VAL_LEN(blk) != 2) {
				Trap_Arg(blk);
			}
			DS_PUSH_NONE;
			if (!MT_Struct(DS_TOP, t, REB_STRUCT)) {
				Trap_Arg(blk);
			}
			if (!rebol_type_to_ffi(out, DS_TOP, n)) {
				Trap_Arg(blk);
			}

			DS_POP;
		} else {
			if (VAL_LEN(blk) != 1) {
				Trap_Arg(blk);
			}
			if (!rebol_type_to_ffi(out, t, n)) {
				Trap_Arg(t);
			}
		}
	} else {
		Trap_Arg(blk);
	}
}

static void callback_dispatcher(ffi_cif *cif, void *ret, void **args, void *user_data)
{
	REBRIN *rin = (REBRIN*)user_data;
	REBINT i = 0;
	REBVAL *blk = NULL;
	REBSER *ser = NULL;
	REBVAL *elem = NULL;

	DS_PUSH_NONE;
	blk = DS_TOP;
	SET_TYPE(blk, REB_BLOCK);
	VAL_SERIES(blk) = ser = Make_Block(1 + cif->nargs);
	VAL_INDEX(blk) = 0;

	elem = Append_Value(ser);
	SET_TYPE(elem, REB_FUNCTION);
	VAL_FUNC(elem) = RIN_FUNC(rin);

	for (i = 0; i < cif->nargs; i ++) {
		elem = Append_Value(ser);
		switch (cif->arg_types[i]->type) {
			case FFI_TYPE_UINT8:
				SET_INTEGER(elem, *(u8*)args[i]);
				break;
			case FFI_TYPE_SINT8:
				SET_INTEGER(elem, *(i8*)args[i]);
				break;
			case FFI_TYPE_UINT16:
				SET_INTEGER(elem, *(u16*)args[i]);
				break;
			case FFI_TYPE_SINT16:
				SET_INTEGER(elem, *(i16*)args[i]);
				break;
			case FFI_TYPE_UINT32:
				SET_INTEGER(elem, *(u32*)args[i]);
				break;
			case FFI_TYPE_SINT32:
				SET_INTEGER(elem, *(i32*)args[i]);
				break;
			case FFI_TYPE_UINT64:
			case FFI_TYPE_POINTER:
				SET_INTEGER(elem, *(u64*)args[i]);
				break;
			case FFI_TYPE_SINT64:
				SET_INTEGER(elem, *(i64*)args[i]);
				break;
			case FFI_TYPE_STRUCT:
				if (!IS_STRUCT((REBVAL*)SERIES_SKIP(RIN_ARGS_STRUCTS(rin), i))) {
					Trap_Arg(elem);
				}
				Copy_Struct_Val(SERIES_SKIP(RIN_ARGS_STRUCTS(rin), i), elem);
				memcpy(SERIES_SKIP(VAL_STRUCT_DATA_BIN(elem), VAL_STRUCT_OFFSET(elem)),
					   args[i],
					   VAL_STRUCT_LEN(elem));
				break;
			default:
				Trap_Arg(elem);
		}
	}
	elem = Do_Blk(ser, 0);
	switch (cif->rtype->type) {
		case FFI_TYPE_VOID:
			break;
		case FFI_TYPE_UINT8:
			*((u8*)ret) = (u8)VAL_INT64(elem);
			break;
		case FFI_TYPE_SINT8:
			*((i8*)ret) = (i8)VAL_INT64(elem);
			break;
		case FFI_TYPE_UINT16:
			*((u16*)ret) = (u16)VAL_INT64(elem);
			break;
		case FFI_TYPE_SINT16:
			*((i16*)ret) = (i16)VAL_INT64(elem);
			break;
		case FFI_TYPE_UINT32:
			*((u32*)ret) = (u32)VAL_INT64(elem);
			break;
		case FFI_TYPE_SINT32:
			*((i32*)ret) = (i32)VAL_INT64(elem);
			break;
		case FFI_TYPE_UINT64:
		case FFI_TYPE_POINTER:
			*((u64*)ret) = (u64)VAL_INT64(elem);
			break;
		case FFI_TYPE_SINT64:
			*((i64*)ret) = (i64)VAL_INT64(elem);
			break;
		case FFI_TYPE_STRUCT:
			memcpy(ret,
				   SERIES_SKIP(VAL_STRUCT_DATA_BIN(elem), VAL_STRUCT_OFFSET(elem)),
				   VAL_STRUCT_LEN(elem));
			break;
		default:
			Trap_Arg(elem);
	}

	DS_POP;
}

/***********************************************************************
**
*/	REBFLG MT_Routine(REBVAL *out, REBVAL *data, REBCNT type)
/*
** format:
** make routine! [[
** 	"document"
** 	arg1 [type1 type2] "note"
** 	arg2 [type3] "note"
** 	...
** 	argn [typen] "note"
** 	return: [type] "note"
** 	abi: word "note"
** ] lib "name"]
**
***********************************************************************/
{
	//RL_Print("%s, %d\n", __func__, __LINE__);
	ffi_type ** args = NULL;
	REBVAL *blk = NULL;
	REBCNT eval_idx = 0; /* for spec block evaluation */
	REBSER *extra_mem = NULL;
	REBFLG ret = TRUE;
	void (*func) (void) = NULL;
	REBCNT n = 1; /* arguments start with the index 1 (return type has a index of 0) */
	REBCNT has_return = 0;
	REBCNT has_abi = 0;

	if (!IS_BLOCK(data)) {
		return FALSE;
	}

	SET_TYPE(out, type);

	VAL_ROUTINE_INFO(out) = Make_Node(RIN_POOL);
	memset(VAL_ROUTINE_INFO(out), 0, sizeof(REBRIN));
	USE_ROUTINE(VAL_ROUTINE_INFO(out));

	if (type == REB_CALLBACK) {
		ROUTINE_SET_FLAG(VAL_ROUTINE_INFO(out), ROUTINE_CALLBACK);
	}

#define N_ARGS 8

	VAL_ROUTINE_SPEC(out) = Copy_Series(VAL_SERIES(data));
	VAL_ROUTINE_FFI_ARGS(out) = Make_Series(N_ARGS, sizeof(ffi_type*), FALSE);
	VAL_ROUTINE_ARGS(out) = Make_Block(N_ARGS);
	Append_Value(VAL_ROUTINE_ARGS(out)); //first word should be 'self', but ignored here.
	VAL_ROUTINE_FFI_ARG_STRUCTS(out) = Make_Block(N_ARGS);
	Append_Value(VAL_ROUTINE_FFI_ARG_STRUCTS(out)); /* reserve for returning struct */

	VAL_ROUTINE_ABI(out) = FFI_DEFAULT_ABI;
	VAL_ROUTINE_LIB(out) = NULL;

	extra_mem = Make_Series(N_ARGS, sizeof(void*), FALSE);
	VAL_ROUTINE_EXTRA_MEM(out) = extra_mem;

	args = (ffi_type**)SERIES_DATA(VAL_ROUTINE_FFI_ARGS(out));
	EXPAND_SERIES_TAIL(VAL_ROUTINE_FFI_ARGS(out), 1); //reserved for return type
	args[0] = &ffi_type_void; //default return type

	init_type_map();

	blk = VAL_BLK_DATA(data);
	if (type == REB_ROUTINE) {
		REBINT fn_idx = 0;
		REBVAL *lib = NULL;
		if (!IS_BLOCK(&blk[0]))
			Trap_Types(RE_EXPECT_VAL, REB_BLOCK, VAL_TYPE(&blk[0]));

		fn_idx = Do_Next(VAL_SERIES(data), 1, 0);
		lib = DS_POP; //Do_Next saves result on stack

		if (IS_INTEGER(lib)) {
			if (NOT_END(&blk[fn_idx])) {
				Trap_Arg(&blk[fn_idx]);
			}
			//treated as a pointer to the function
			if (VAL_INT64(lib) == 0) {
				Trap_Arg(lib);
			}
			VAL_ROUTINE_FUNCPTR(out) = (void (*) (void))VAL_INT64(lib);
		} else {
			if (!IS_LIBRARY(lib))
				Trap_Arg(lib);

			if (!IS_STRING(&blk[fn_idx]))
				Trap_Arg(&blk[fn_idx]);

			if (NOT_END(&blk[fn_idx + 1])) {
				Trap_Arg(&blk[fn_idx + 1]);
			}

			VAL_ROUTINE_LIB(out) = VAL_LIB_HANDLE(lib);
			if (!VAL_ROUTINE_LIB(out)) {
				Trap_Arg(lib);
				//RL_Print("lib is not open\n");
				ret = FALSE;
			}
			TERM_SERIES(VAL_SERIES(&blk[fn_idx]));
			func = OS_FIND_FUNCTION(LIB_FD(VAL_ROUTINE_LIB(out)), VAL_DATA(&blk[fn_idx]));
			if (!func) {
				Trap_Arg(&blk[fn_idx]);
				//printf("Couldn't find function: %s\n", VAL_DATA(&blk[2]));
				ret = FALSE;
			} else {
				VAL_ROUTINE_FUNCPTR(out) = func;
			}
		}
	} else if (type == REB_CALLBACK) {
		REBINT fn_idx = 0;
		REBVAL *fun = NULL;
		if (!IS_BLOCK(&blk[0]))
			Trap_Arg(&blk[0]);
		fn_idx = Do_Next(VAL_SERIES(data), 1, 0);
		fun = DS_POP; //Do_Next saves result on stack
		if (!IS_FUNCTION(fun))
			Trap_Arg(fun);
		VAL_CALLBACK_FUNC(out) = VAL_FUNC(fun);
		if (NOT_END(&blk[fn_idx])) {
			Trap_Arg(&blk[fn_idx]);
		}
		//printf("RIN: %p, func: %p\n", VAL_ROUTINE_INFO(out), &blk[1]);
	}

	blk = VAL_BLK_DATA(&blk[0]);
	if (NOT_END(blk) && IS_STRING(blk)) {
		++ blk;
	}
	while (NOT_END(blk)) {
		switch (VAL_TYPE(blk)) {
			case REB_WORD:
				{
					if (VAL_WORD_CANON(blk) == SYM_ELLIPSIS) {
						REBVAL *v = NULL;
						if (ROUTINE_GET_FLAG(VAL_ROUTINE_INFO(out), ROUTINE_VARARGS)) {
							Trap_Arg(blk); /* duplicate ellipsis */
						}
						ROUTINE_SET_FLAG(VAL_ROUTINE_INFO(out), ROUTINE_VARARGS);
						//Change the argument list to be a block
						VAL_ROUTINE_FIXED_ARGS(out) = Copy_Series(VAL_ROUTINE_ARGS(out));
						Remove_Series(VAL_ROUTINE_ARGS(out), 1, SERIES_TAIL(VAL_ROUTINE_ARGS(out)));
						v = Append_Value(VAL_ROUTINE_ARGS(out));
						Init_Word(v, SYM_VARARGS);
						TYPE_SET(v, REB_BLOCK);
					} else {
						REBVAL *v = NULL;
						if (ROUTINE_GET_FLAG(VAL_ROUTINE_INFO(out), ROUTINE_VARARGS)) {
							//... has to be the last argument
							Trap_Arg(blk);
						}
						v = Append_Value(VAL_ROUTINE_ARGS(out));
						Init_Word(v, VAL_WORD_SYM(blk));
						EXPAND_SERIES_TAIL(VAL_ROUTINE_FFI_ARGS(out), 1);

						++ blk;
						process_type_block(out, blk, n);
					}
				}
				n ++;
				break;
			case REB_SET_WORD:
				switch (VAL_WORD_CANON(blk)) {
					case SYM_ABI:
						++ blk;
						if (!IS_WORD(blk) || has_abi > 1) {
							Trap_Arg(blk);
						}
						switch (VAL_WORD_CANON(blk)) {
							case SYM_DEFAULT:
								VAL_ROUTINE_ABI(out) = FFI_DEFAULT_ABI;
								break;
#ifdef X86_WIN64
							case SYM_WIN64:
								VAL_ROUTINE_ABI(out) = FFI_WIN64;
								break;
#elif defined(X86_WIN32) || defined(TO_LINUX_X86) || defined(TO_LINUX_X64)
							case SYM_STDCALL:
								VAL_ROUTINE_ABI(out) = FFI_STDCALL;
								break;
							case SYM_SYSV:
								VAL_ROUTINE_ABI(out) = FFI_SYSV;
								break;
							case SYM_THISCALL:
								VAL_ROUTINE_ABI(out) = FFI_THISCALL;
								break;
							case SYM_FASTCALL:
								VAL_ROUTINE_ABI(out) = FFI_FASTCALL;
								break;
#ifdef X86_WIN32
							case SYM_MS_CDECL:
								VAL_ROUTINE_ABI(out) = FFI_MS_CDECL;
								break;
#else
							case SYM_UNIX64:
								VAL_ROUTINE_ABI(out) = FFI_UNIX64;
								break;
#endif //X86_WIN32
#elif defined (TO_LINUX_ARM)
							case SYM_VFP:
								VAL_ROUTINE_ABI(out) = FFI_VFP;
							case SYM_SYSV:
								VAL_ROUTINE_ABI(out) = FFI_SYSV;
								break;
#elif defined (TO_LINUX_MIPS)
							case SYM_O32:
								VAL_ROUTINE_ABI(out) = FFI_O32;
								break;
							case SYM_N32:
								VAL_RNUTINE_ABI(out) = FFI_N32;
								break;
							case SYM_N64:
								VAL_RNUTINE_ABI(out) = FFI_N64;
								break;
							case SYM_O32_SOFT_FLOAT:
								VAL_ROUTINE_ABI(out) = FFI_O32_SOFT_FLOAT;
								break;
							case SYM_N32_SOFT_FLOAT:
								VAL_RNUTINE_ABI(out) = FFI_N32_SOFT_FLOAT;
								break;
							case SYM_N64_SOFT_FLOAT:
								VAL_RNUTINE_ABI(out) = FFI_N64_SOFT_FLOAT;
								break;
#endif //X86_WIN64
							default:
								Trap_Arg(blk);
						}
						has_abi ++;
						break;
					case SYM_RETURN:
						if (has_return > 1) {
							Trap_Arg(blk);
						}
						has_return ++;
						++ blk;
						process_type_block(out, blk, 0);
						break;
					default:
						Trap_Arg(blk);
				}
				break;
			default:
				Trap_Arg(blk);
		}
		++ blk;
		if (IS_STRING(blk)) { /* notes, ignoring */
			++ blk;
		}
	}

	if (!ROUTINE_GET_FLAG(VAL_ROUTINE_INFO(out), ROUTINE_VARARGS)) {
		VAL_ROUTINE_CIF(out) = OS_MAKE(sizeof(ffi_cif));
		//printf("allocated cif at: %p\n", VAL_ROUTINE_CIF(out));
		QUEUE_EXTRA_MEM(VAL_ROUTINE_INFO(out), VAL_ROUTINE_CIF(out));

		/* series data could have moved */
		args = (ffi_type**)SERIES_DATA(VAL_ROUTINE_FFI_ARGS(out));
		if (FFI_OK != ffi_prep_cif((ffi_cif*)VAL_ROUTINE_CIF(out),
				VAL_ROUTINE_ABI(out),
				SERIES_TAIL(VAL_ROUTINE_FFI_ARGS(out)) - 1,
				args[0],
				&args[1])) {
			//RL_Print("Couldn't prep CIF\n");
			ret = FALSE;
		}
	}

	if (type == REB_CALLBACK) {
		VAL_ROUTINE_CLOSURE(out) = ffi_closure_alloc(sizeof(ffi_closure), (void**)&VAL_ROUTINE_DISPATCHER(out));
		if (VAL_ROUTINE_CLOSURE(out) == NULL) {
			//printf("No memory\n");
			ret = FALSE;
		} else {
			if (FFI_OK != ffi_prep_closure_loc(VAL_ROUTINE_CLOSURE(out),
											   VAL_ROUTINE_CIF(out),
											   callback_dispatcher,
											   VAL_ROUTINE_INFO(out),
											   VAL_ROUTINE_DISPATCHER(out))) {
				//RL_Print("Couldn't prep closure\n");
				ret = FALSE;
			}
		}
	}

	//RL_Print("%s, %d, ret = %d\n", __func__, __LINE__, ret);
	return ret;
}

/***********************************************************************
**
*/	REBTYPE(Routine)
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
			//RL_Print("%s, %d, Make routine action\n", __func__, __LINE__);
		case A_TO:
			if (IS_ROUTINE(val)) {
				Trap_Types(RE_EXPECT_VAL, REB_ROUTINE, VAL_TYPE(arg));
			} else if (!IS_BLOCK(arg) || !MT_Routine(ret, arg, REB_ROUTINE)) {
				Trap_Types(RE_EXPECT_VAL, REB_BLOCK, VAL_TYPE(arg));
			}
			break;
		case A_REFLECT:
			{
				REBINT n = VAL_WORD_CANON(arg); // zero on error
				switch (n) {
					case SYM_SPEC:
						Set_Block(ret, Clone_Block(VAL_ROUTINE_SPEC(val)));
						Unbind_Block(VAL_BLK(val), TRUE);
						break;
					case SYM_ADDR:
						SET_INTEGER(ret, (REBUPT)VAL_ROUTINE_FUNCPTR(val));
						break;
					default:
						Trap_Reflect(REB_STRUCT, arg);
				}
			}
			break;
		default:
			Trap_Action(REB_ROUTINE, action);
	}
	return R_RET;
}

/***********************************************************************
**
*/	REBTYPE(Callback)
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
			//RL_Print("%s, %d, Make routine action\n", __func__, __LINE__);
		case A_TO:
			if (IS_ROUTINE(val)) {
				Trap_Types(RE_EXPECT_VAL, REB_ROUTINE, VAL_TYPE(arg));
			} else if (!IS_BLOCK(arg) || !MT_Routine(ret, arg, REB_CALLBACK)) {
				Trap_Types(RE_EXPECT_VAL, REB_BLOCK, VAL_TYPE(arg));
			}
			break;
		case A_REFLECT:
			{
				REBINT n = VAL_WORD_CANON(arg); // zero on error
				switch (n) {
					case SYM_SPEC:
						Set_Block(ret, Clone_Block(VAL_ROUTINE_SPEC(val)));
						Unbind_Block(VAL_BLK(val), TRUE);
						break;
					case SYM_ADDR:
						SET_INTEGER(ret, (REBUPT)VAL_ROUTINE_DISPATCHER(val));
						break;
					default:
						Trap_Reflect(REB_STRUCT, arg);
				}
			}
			break;
		default:
			Trap_Action(REB_CALLBACK, action);
	}
	return R_RET;
}
