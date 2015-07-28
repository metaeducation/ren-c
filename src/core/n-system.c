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
**  Module:  n-system.c
**  Summary: native functions for system operations
**  Section: natives
**  Author:  Carl Sassenrath
**  Notes:
**    GC WARNING: Do not cache pointer to stack ARGS (stack may expand).
**
***********************************************************************/

#include "sys-core.h"


/***********************************************************************
**
*/	REBNATIVE(halt)
/*
***********************************************************************/
{
	Halt();
	DEAD_END;
}


/***********************************************************************
**
*/	REBNATIVE(quit)
/*
**		1: /return
**		2: value
**
***********************************************************************/
{
	if (D_REF(1)) {
		assert(IS_INTEGER(D_ARG(2)));
		Quit(Int32(D_ARG(2)));
	}
	else
		Quit(0);

	DEAD_END;
}


/***********************************************************************
**
*/	REBNATIVE(recycle)
/*
***********************************************************************/
{
	REBCNT count;

	if (D_REF(1)) { // /off
		GC_Active = FALSE;
		return R_UNSET;
	}

	if (D_REF(2)) {// /on
		GC_Active = TRUE;
		SET_INT32(TASK_BALLAST, VAL_INT32(TASK_MAX_BALLAST));
	}

	if (D_REF(3)) {// /ballast
		*TASK_MAX_BALLAST = *D_ARG(4);
		SET_INT32(TASK_BALLAST, VAL_INT32(TASK_MAX_BALLAST));
	}

	if (D_REF(5)) { // torture
		GC_Active = TRUE;
		SET_INT32(TASK_BALLAST, 0);
	}

	count = Recycle();

	SET_INTEGER(D_OUT, count);
	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(stats)
/*
***********************************************************************/
{
	REBI64 n;
	REBCNT flags = 0;
	REBVAL *stats;

	if (D_REF(3)) {
		VAL_TIME(D_OUT) = OS_DELTA_TIME(PG_Boot_Time, 0) * 1000;
		VAL_SET(D_OUT, REB_TIME);
		return R_OUT;
	}

	if (D_REF(4)) {
		n = Eval_Cycles + Eval_Dose - Eval_Count;
		SET_INTEGER(D_OUT, n);
		return R_OUT;
	}

	if (D_REF(2)) {
		stats = Get_System(SYS_STANDARD, STD_STATS);
		*D_OUT = *stats;
		if (IS_OBJECT(stats)) {
			stats = Get_Object(stats, 1);

			VAL_TIME(stats) = OS_DELTA_TIME(PG_Boot_Time, 0) * 1000;
			VAL_SET(stats, REB_TIME);
			stats++;
			SET_INTEGER(stats, Eval_Cycles + Eval_Dose - Eval_Count);
			stats++;
			SET_INTEGER(stats, Eval_Natives);
			stats++;
			SET_INTEGER(stats, Eval_Functions);

			stats++;
			SET_INTEGER(stats, PG_Reb_Stats->Series_Made);
			stats++;
			SET_INTEGER(stats, PG_Reb_Stats->Series_Freed);
			stats++;
			SET_INTEGER(stats, PG_Reb_Stats->Series_Expanded);
			stats++;
			SET_INTEGER(stats, PG_Reb_Stats->Series_Memory);
			stats++;
			SET_INTEGER(stats, PG_Reb_Stats->Recycle_Series_Total);

			stats++;
			SET_INTEGER(stats, PG_Reb_Stats->Blocks);
			stats++;
			SET_INTEGER(stats, PG_Reb_Stats->Objects);

			stats++;
			SET_INTEGER(stats, PG_Reb_Stats->Recycle_Counter);
		}
		return R_OUT;
	}

	if (D_REF(5)) {
		REBVAL *pool_id = D_ARG(6);
		Dump_Series_In_Pool(VAL_INT32(pool_id));
		return R_NONE;
	}

	if (D_REF(1)) flags = 3;
	n = Inspect_Series(flags);

	SET_INTEGER(D_OUT, n);

	return R_OUT;
}

const char *evoke_help = "Evoke values:\n"
	"[stack-size n] crash-dump delect\n"
	"watch-recycle watch-obj-copy crash\n"
	"1: watch expand\n"
	"2: check memory pools\n"
	"3: check bind table\n"
;

/***********************************************************************
**
*/	REBNATIVE(evoke)
/*
***********************************************************************/
{
	REBVAL *arg = D_ARG(1);
	REBCNT len;

	Check_Security(SYM_DEBUG, POL_READ, 0);

	if (IS_BLOCK(arg)) {
		len = VAL_LEN(arg);
		arg = VAL_BLK_DATA(arg);
	}
	else len = 1;

	for (; len > 0; len--, arg++) {
		if (IS_WORD(arg)) {
			switch (VAL_WORD_CANON(arg)) {
			case SYM_DELECT:
				Trace_Delect(1);
				break;
			case SYM_CRASH_DUMP:
				Reb_Opts->crash_dump = TRUE;
				break;
			case SYM_WATCH_RECYCLE:
				Reb_Opts->watch_recycle = !Reb_Opts->watch_recycle;
				break;
			case SYM_WATCH_OBJ_COPY:
				Reb_Opts->watch_obj_copy = !Reb_Opts->watch_obj_copy;
				break;
			case SYM_STACK_SIZE:
				arg++;
				Expand_Stack(Int32s(arg, 1));
				break;
			case SYM_CRASH:
				Panic_DEAD_END(RP_MISC);
				break;
			default:
				Out_Str(cb_cast(evoke_help), 1);
			}
		}
		if (IS_INTEGER(arg)) {
			switch (Int32(arg)) {
			case 0:
				Check_Memory();
				Check_Bind_Table();
				break;
			case 1:
				Reb_Opts->watch_expand = TRUE;
				break;
			case 2:
				Check_Memory();
				break;
			case 3:
				Check_Bind_Table();
				break;
			default:
				Out_Str(cb_cast(evoke_help), 1);
			}
		}
	}

	return R_UNSET;
}

#ifdef NOT_USED
/***********************************************************************
**
*/	REBNATIVE(in_context)
/*
***********************************************************************/
{
	REBVAL *value;
	value = D_ARG(1);
	VAL_OBJ_FRAME(ROOT_USER_CONTEXT) = VAL_OBJ_FRAME(value);
	return R_UNSET;
}
#endif

/***********************************************************************
**
*/	REBNATIVE(limit_usage)
/*
***********************************************************************/
{
	REBCNT sym;

	sym = VAL_WORD_CANON(D_ARG(1));

	// Only gets set once:
	if (sym == SYM_EVAL) {
		if (Eval_Limit == 0) Eval_Limit = Int64(D_ARG(2));
	} else if (sym == SYM_MEMORY) {
		if (PG_Mem_Limit == 0) PG_Mem_Limit = Int64(D_ARG(2));
	}
	return R_UNSET;
}


/***********************************************************************
**
*/	REBNATIVE(stack)
/*
**	stack: native [
**		{Returns stack backtrace or other values.}
**		offset [integer!] "Relative backward offset"
**		/block "Block evaluation position"
**		/word "Function or object name, if known"
**		/func "Function value"
**		/args "Block of args (may be modified)"
**		/size "Current stack size (in value units)"
**		/depth "Stack depth (frames)"
**		/limit "Stack bounds (auto expanding)"
**	]
**
***********************************************************************/
{
	REBINT index = VAL_INT32(D_ARG(1));
	REBVAL *sp;
	REBCNT len;

	Check_Security(SYM_DEBUG, POL_READ, 0);

	sp = Stack_Frame(index);
	if (!sp) return R_NONE;

	if (D_REF(2)) *D_OUT = sp[1];		// block
	else if (D_REF(3)) {
		Init_Word_Unbound(D_OUT, REB_WORD, VAL_WORD_SYM(sp+2));	// word
	}
	else if (D_REF(4)) *D_OUT = sp[3];	// func
	else if (D_REF(5)) {		// args
		len = 0;
		if (ANY_FUNC(sp+3)) len = VAL_FUNC_ARGC(sp+3)-1;
		sp += 4;
		Set_Block(D_OUT, Copy_Values(sp, len));
	}
	else if (D_REF(6)) {		// size
		SET_INTEGER(D_OUT, DSP+1);
	}
	else if (D_REF(7)) {		// depth
		SET_INTEGER(D_OUT, Stack_Depth());
	}
	else if (D_REF(8)) {		// limit
		SET_INTEGER(D_OUT, SERIES_REST(DS_Series) + SERIES_BIAS(DS_Series));
	}
	else {
		Set_Block(D_OUT, Make_Backtrace(index));
	}

	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(check)
/*
***********************************************************************/
{
	REBVAL *val;
	REBSER *ser;
	REBCNT n;

	ser = VAL_SERIES(val = D_ARG(1));
	*D_OUT = *val;

	if (ANY_BLOCK(val)) {
		for (n = 0; n < SERIES_TAIL(ser); n++) {
			if (IS_END(BLK_SKIP(ser, n))) goto err;
		}
		if (!IS_END(BLK_SKIP(ser, n))) goto err;
	}
	else {
		for (n = 0; n < SERIES_TAIL(ser); n++) {
			if (!*STR_SKIP(ser, n)) goto err;
		}
		if (*STR_SKIP(ser, n)) goto err;
	}
	return R_OUT;
err:
	Trap_DEAD_END(RE_BAD_SERIES);
	DEAD_END;
}


/***********************************************************************
**
*/	REBNATIVE(ds)
/*
***********************************************************************/
{
	Dump_Stack(0, 0);
	return R_UNSET;
}


/***********************************************************************
**
*/	REBNATIVE(do_codec)
/*
**		Calls a codec handle with specific data:
**
**	Args:
**		1: codec:  handle!
**		2: action: word! (identify, decode, encode)
**		3: data:   binary! image! sound!
**		4: option: (optional)
**
***********************************************************************/
{
	REBCDI codi;
	REBVAL *val;
	REBINT result;
	REBSER *ser;

	CLEAR(&codi, sizeof(codi));

	codi.action = CODI_ACT_DECODE;

	val = D_ARG(3);

	switch (VAL_WORD_SYM(D_ARG(2))) {

	case SYM_IDENTIFY:
		codi.action = CODI_ACT_IDENTIFY;
	case SYM_DECODE:
		if (!IS_BINARY(val)) Trap1_DEAD_END(RE_INVALID_ARG, val);
		codi.data = VAL_BIN_DATA(D_ARG(3));
		codi.len  = VAL_LEN(D_ARG(3));
		break;

	case SYM_ENCODE:
		codi.action = CODI_ACT_ENCODE;
		if (IS_IMAGE(val)) {
			codi.extra.bits = VAL_IMAGE_BITS(val);
			codi.w = VAL_IMAGE_WIDE(val);
			codi.h = VAL_IMAGE_HIGH(val);
			codi.alpha = Image_Has_Alpha(val, 0);
		} else if (IS_STRING(val)) {
			codi.w = VAL_SERIES_WIDTH(val);
			codi.len = VAL_LEN(val);
			codi.extra.other = VAL_BIN_DATA(val);
		}
		else
			Trap1_DEAD_END(RE_INVALID_ARG, val);
		break;

	default:
		Trap1_DEAD_END(RE_INVALID_ARG, D_ARG(2));
	}

	// Nasty alias, but it must be done:
	// !!! add a check to validate the handle as a codec!!!!
	result = cast(codo, VAL_HANDLE_CODE(D_ARG(1)))(&codi);

	if (codi.error != 0) {
		if (result == CODI_CHECK) return R_FALSE;
		Trap_DEAD_END(RE_BAD_MEDIA); // need better!!!
	}

	switch (result) {

	case CODI_CHECK:
		return R_TRUE;

	case CODI_TEXT: //used on decode
		switch (codi.w) {
			default: /* some decoders might not set this field */
			case 1:
				ser = Make_Binary(codi.len);
				break;
			case 2:
				ser = Make_Unicode(codi.len);
				break;
		}
		memcpy(BIN_HEAD(ser), codi.data, codi.w? (codi.len * codi.w) : codi.len);
		ser->tail = codi.len;
		Set_String(D_OUT, ser);
		break;

	case CODI_BINARY: //used on encode
		ser = Make_Binary(codi.len);
		ser->tail = codi.len;

		// optimize for pass-thru decoders, which leave codi.data NULL
		memcpy(
			BIN_HEAD(ser),
			codi.data ? codi.data : codi.extra.other,
			codi.len
		);
		Set_Binary(D_OUT, ser);

		//don't free the text binary input buffer during decode (it's the 3rd arg value in fact)
		// See notice in reb-codec.h on reb_codec_image
		if (codi.data) {
			FREE_ARRAY(REBYTE, codi.len, codi.data);
		}
		break;

	case CODI_IMAGE: //used on decode
		ser = Make_Image(codi.w, codi.h, TRUE); // Puts it into RETURN stack position
		memcpy(IMG_DATA(ser), codi.extra.bits, codi.w * codi.h * 4);
		SET_IMAGE(D_OUT, ser);

		// See notice in reb-codec.h on reb_codec_image
		FREE_ARRAY(u32, codi.w * codi.h, codi.extra.bits);
		break;

	case CODI_BLOCK:
		Set_Block(D_OUT, cast(REBSER*, codi.extra.other));
		break;

	default:
		Trap_DEAD_END(RE_BAD_MEDIA); // need better!!!
	}

	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(selflessq)
/*
***********************************************************************/
{
	REBVAL *val = D_ARG(1);
	REBSER *frm;

	if (ANY_WORD(val)) {
		if (VAL_WORD_INDEX(val) < 0) return R_TRUE;
		frm = VAL_WORD_FRAME(val);
		if (!frm) Trap1_DEAD_END(RE_NOT_DEFINED, val);
	}
	else frm = VAL_OBJ_FRAME(D_ARG(1));

	return IS_SELFLESS(frm) ? R_TRUE : R_FALSE;
}
