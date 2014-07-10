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

#include "sys-core.h"

#include <ffi.h>

static ffi_type * struct_type_to_ffi [STRUCT_TYPE_MAX];

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
	RL_Print("%s, %d\n", __func__, __LINE__);
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

static ffi_type* struct_to_ffi(REBVAL *out, REBVAL *elem)
{
	ffi_type *args = (ffi_type*) SERIES_DATA(VAL_ROUTINE_ARGS(out));
	REBSER *fields = VAL_STRUCT_FIELDS(elem);
	REBSER *extra_mem = VAL_ROUTINE_EXTRA_MEM(out);
	REBCNT i = 0, j = 0;
	REBCNT n_basic_type = 0;

	ffi_type *stype = OS_MAKE(sizeof(ffi_type));
	printf("allocated stype at: %p\n", stype);
	*(void**) SERIES_SKIP(extra_mem, SERIES_TAIL(extra_mem)) = stype;
	EXPAND_SERIES_TAIL(extra_mem, 1);

	stype->size = stype->alignment = 0;
	stype->type = FFI_TYPE_STRUCT;

	stype->elements = OS_MAKE(sizeof(ffi_type *) * (1 + n_struct_fields(VAL_STRUCT_FIELDS(elem)))); /* one extra for NULL */
	*(void**) SERIES_SKIP(extra_mem, SERIES_TAIL(extra_mem)) = stype->elements;
	printf("allocated stype elements at: %p\n", stype->elements);
	EXPAND_SERIES_TAIL(extra_mem, 1);

	for (i = 0; i < SERIES_TAIL(fields); i ++) {
		struct Struct_Field *field = (struct Struct_Field*)SERIES_SKIP(fields, i);
		if (field->type != STRUCT_TYPE_STRUCT) {
			if (struct_type_to_ffi[field->type]) {
				REBINT n = 0;
				for (n = 0; n < field->dimension; n ++) {
					stype->elements[j++] = struct_type_to_ffi[field->type];
				}
			} else {
				return NULL;
			}
		} else {
			ffi_type *subtype = struct_to_ffi(out, elem);
			if (subtype) {
				REBINT n = 0;
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
	ffi_type **args = (ffi_type**) SERIES_DATA(VAL_ROUTINE_ARGS(out));
	if (IS_WORD(elem)) {
		switch (VAL_WORD_CANON(elem)) {
			case SYM_UINT8:
				args[idx] = &ffi_type_uint8;
				break;
			case SYM_INT8:
				args[idx] = &ffi_type_sint8;
				break;
			case SYM_UINT16:
				args[idx] = &ffi_type_uint16;
				break;
			case SYM_INT16:
				args[idx] = &ffi_type_sint16;
				break;
			case SYM_UINT32:
				args[idx] = &ffi_type_uint32;
				break;
			case SYM_INT32:
				args[idx] = &ffi_type_sint32;
				break;
			case SYM_UINT64:
				args[idx] = &ffi_type_uint64;
				break;
			case SYM_INT64:
				args[idx] = &ffi_type_sint64;
				break;
			case SYM_FLOAT:
				args[idx] = &ffi_type_float;
				break;
			case SYM_DOUBLE:
				args[idx] = &ffi_type_double;
				break;
			case SYM_POINTER:
				args[idx] = &ffi_type_pointer;
				break;
			default:
				return FALSE;
		}
	} else if (IS_STRUCT(elem)) {
		ffi_type *ftype = struct_to_ffi(out, elem);
		if (ftype) {
			args[idx] = ftype;
		} else {
			return FALSE;
		}
	} else {
		return FALSE;
	}
	return TRUE;
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
		printf("freeing %p\n", addr);
		OS_FREE(addr);
	}

	UNMARK_ROUTINE(rin);
	Free_Node(RIN_POOL, (REBNOD*)rin);
}


/***********************************************************************
**
*/	REBFLG MT_Routine(REBVAL *out, REBVAL *data, REBCNT type)
/*
***********************************************************************/
{
	RL_Print("%s, %d\n", __func__, __LINE__);
	ffi_type ** args = NULL;
	REBVAL *blk = NULL;
	REBCNT eval_idx = 0; /* for spec block evaluation */
	REBVAL *val = NULL;
	REBVAL *name = NULL; //function name
	REBSER *extra_mem = NULL;
	REBFLG ret = TRUE;

	if (!IS_BLOCK(data)) {
		return FALSE;
	}

	DS_PUSH_NONE; //a tmp value
	val = DS_TOP;

	DS_PUSH_NONE;
	name = DS_TOP;

	SET_TYPE(out, REB_ROUTINE);

	VAL_ROUTINE_INFO(out) = Make_Node(RIN_POOL);
	USE_ROUTINE(VAL_ROUTINE_INFO(out));

	VAL_ROUTINE_SPEC(out) = Copy_Series(VAL_SERIES(data));
	VAL_ROUTINE_ARGS(out) = Make_Series(8, sizeof(ffi_type*), FALSE);
	VAL_ROUTINE_ABI(out) = FFI_DEFAULT_ABI;
	VAL_ROUTINE_LIB(out) = NULL;

	extra_mem = Make_Series(8, sizeof(void*), FALSE);
	VAL_ROUTINE_EXTRA_MEM(out) = extra_mem;

	args = (ffi_type**)SERIES_DATA(VAL_ROUTINE_ARGS(out));
	EXPAND_SERIES_TAIL(VAL_ROUTINE_ARGS(out), 1); //reserved for return type
	args[0] = &ffi_type_void;

	init_type_map();

	blk = VAL_BLK_DATA(data);
	while (NOT_END(blk)) {
		if (IS_SET_WORD(blk)) {
			REBINT cat = VAL_WORD_CANON(blk);
			switch (VAL_WORD_CANON(blk)) {
				case SYM_LIBRARY:
				case SYM_NAME:
				case SYM_ABI:
				case SYM_RETURN:
					++ blk;
					eval_idx = blk - VAL_BLK_DATA(data);

					eval_idx = Do_Next(VAL_SERIES(data), eval_idx, 0);

					blk = VAL_BLK_SKIP(data, eval_idx);
					val = DS_POP; //Do_Next saves result on stack
					break;
				default:
					Trap_Arg(blk);
			}

			switch (cat) {
				case SYM_LIBRARY:
					if (!IS_LIBRARY(val)) {
						Trap_Types(RE_EXPECT_VAL, REB_LIBRARY, VAL_TYPE(val));
					}
					VAL_ROUTINE_LIB(out) = VAL_LIB_HANDLE(val);
					break;
				case SYM_NAME:
					if (!IS_STRING(val)) {
						Trap_Arg(val);
					}
					*name = *val;
					break;
				case SYM_ABI:
					if (IS_WORD(val)) {
						switch (VAL_WORD_CANON(val)) {
							case SYM_DEFAULT:
								VAL_ROUTINE_ABI(out) = FFI_DEFAULT_ABI;
								break;
							case SYM_STDCALL:
								VAL_ROUTINE_ABI(out) = FFI_STDCALL;
								break;
							case SYM_UNIX64:
								VAL_ROUTINE_ABI(out) = FFI_UNIX64;
								break;
#ifdef X86_WIN64
							case SYM_WIN64:
								VAL_ROUTINE_ABI(out) = FFI_WIN64;
								break;
#endif
#ifdef X86_WIN32
							case SYM_MS_CDECL:
								VAL_ROUTINE_ABI(out) = FFI_MS_CDECL;
								break;
#endif
							case SYM_SYSV:
								VAL_ROUTINE_ABI(out) = FFI_SYSV;
								break;
							case SYM_THISCALL:
								VAL_ROUTINE_ABI(out) = FFI_THISCALL;
								break;
							case SYM_FASTCALL:
								VAL_ROUTINE_ABI(out) = FFI_FASTCALL;
								break;
							default:
								Trap_Arg(val);
						}
					} else {
						Trap_Arg(val);
					}
					break;
				case SYM_RETURN:
					if (!IS_NONE(val)) {
						if (!rebol_type_to_ffi(out, val, 0)) {
							Trap_Arg(val);
						}
					}
					break;
			   default:
				   Trap_Arg(blk);
		   }
		} else if (IS_BLOCK(blk)) {
			REBCNT n = 0;
			//Reduce_Block(VAL_SERIES(blk), 0, NULL); //result is on stack
			//val = DS_POP;
			for (n = 0; n < VAL_LEN(blk); n ++) {
				REBVAL *elem = VAL_BLK_SKIP(blk, n);
				EXPAND_SERIES_TAIL(VAL_ROUTINE_ARGS(out), 1);
				if (!rebol_type_to_ffi(out, elem, n + 1)) {
					Trap_Arg(elem);
				}
			}
			++ blk;
		} else {
			Trap_Type(blk);
		}
	}

	if (!VAL_ROUTINE_LIB(out) || IS_NONE(name)) {
		RL_Print("lib is not open or name is null\n");
		ret = FALSE;
	}
	TERM_SERIES(VAL_SERIES(name));
	FUNCPTR func = OS_FIND_FUNCTION(LIB_FD(VAL_ROUTINE_LIB(out)), VAL_DATA(name));
	if (!func) {
		RL_Print("Couldn't find function\n");
		ret = FALSE;
	} else {

		VAL_ROUTINE_CIF(out) = OS_MAKE(sizeof(ffi_cif));
		printf("allocated cif at: %p\n", VAL_ROUTINE_CIF(out));
		*(void**) SERIES_SKIP(extra_mem, SERIES_TAIL(extra_mem)) = VAL_ROUTINE_CIF(out);
		EXPAND_SERIES_TAIL(extra_mem, 1);

		if (FFI_OK != ffi_prep_cif((ffi_cif*)VAL_ROUTINE_CIF(out),
								   VAL_ROUTINE_ABI(out),
								   SERIES_TAIL(VAL_ROUTINE_ARGS(out)) - 1,
								   args[0],
								   &args[1])) {
			RL_Print("Couldn't prep CIF\n");
			ret = FALSE;
		}
	}

	DS_POP; //name
	DS_POP; //val
	RL_Print("%s, %d, ret = %d\n", __func__, __LINE__, ret);
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
			RL_Print("%s, %d, Make routine action\n", __func__, __LINE__);
		case A_TO:
			if (IS_ROUTINE(val)) {
				Trap_Types(RE_EXPECT_VAL, REB_ROUTINE, VAL_TYPE(arg));
			} else if (!IS_BLOCK(arg) || !MT_Routine(ret, arg, REB_ROUTINE)) {
				Trap_Types(RE_EXPECT_VAL, REB_BLOCK, VAL_TYPE(arg));
			}
			break;
		default:
			Trap_Action(REB_ROUTINE, action);
	}
	return R_RET;
}
