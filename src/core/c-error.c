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
**  Module:  c-error.c
**  Summary: error handling
**  Section: core
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/


#include "sys-core.h"


/***********************************************************************
**
*/	void Push_Trap_Helper(REBOL_STATE *s)
/*
**		Used by both TRY and TRY_ANY, whose differentiation comes
**		from how they react to HALT.
**
***********************************************************************/
{
	assert(Saved_State || (DSP == -1 && !DSF));

	s->dsp = DSP;
	s->dsf = DSF;

	s->series_guard_tail = GC_Series_Guard->tail;
	s->value_guard_tail = GC_Value_Guard->tail;
	s->gc_disable = GC_Disabled;

	s->manuals_tail = SERIES_TAIL(GC_Manuals);

	s->last_state = Saved_State;
	Saved_State = s;

	// !!! garbage collector should probably walk Saved_State stack to
	// keep the error values alive from GC, so use a "safe" trash.
	SET_TRASH_SAFE(&s->error);
}


/***********************************************************************
**
*/	REBOOL Trapped_Helper_Halted(REBOL_STATE *state)
/*
**		This is used by both PUSH_TRAP and PUSH_UNHALTABLE_TRAP to do
**		the work of responding to a longjmp.  (Hence it is run when
**		setjmp returns TRUE.)  Its job is to safely recover from
**		a sudden interruption, though the list of things which can
**		be safely recovered from is finite.  Among the countless
**		things that are not handled automatically would be a memory
**		allocation.
**
**		(Note: This is a crucial difference between C and C++, as
**		C++ will walk up the stack at each level and make sure
**		any constructors have their associated destructors run.
**		*Much* safer for large systems, though not without cost.
**		Rebol's greater concern is not so much the cost of setup
**		for stack unwinding, but being able to be compiled without
**		requiring a C++ compiler.)
**
**		Returns whether the trapped error was a RE_HALT or not.
**
***********************************************************************/
{
	struct Reb_Call *call = CS_Top;
	REBOOL halted;

	// You're only supposed to throw an error.
	assert(IS_ERROR(&state->error));

	halted = VAL_ERR_NUM(&state->error) == RE_HALT;

	// Restore Rebol call stack frame at time of Push_Trap
	while (call != state->dsf) {
		struct Reb_Call *prior = call->prior;
		Free_Call(call);
		call = prior;
	}
	SET_DSF(state->dsf);

	// Restore Rebol data stack pointer at time of Push_Trap
	DS_DROP_TO(state->dsp);

	// Free any manual series that were extant at the time of the error
	// (that were created since this PUSH_TRAP started)
	assert(GC_Manuals->tail >= state->manuals_tail);
	while (GC_Manuals->tail != state->manuals_tail) {
		// Freeing the series will update the tail...
		Free_Series(cast(REBSER**, GC_Manuals->data)[GC_Manuals->tail - 1]);
	}

	GC_Series_Guard->tail = state->series_guard_tail;
	GC_Value_Guard->tail = state->value_guard_tail;

	GC_Disabled = state->gc_disable;

	Saved_State = state->last_state;

	return halted;
}


/***********************************************************************
**
*/	void Convert_Name_To_Thrown_Debug(REBVAL *name, const REBVAL *arg)
/*
**		Debug-only version of CONVERT_NAME_TO_THROWN
**
**		Sets a task-local value to be associated with the name and
**		mark it as the proxy value indicating a THROW().
**
***********************************************************************/
{
	assert(!THROWN(name));
	VAL_SET_OPT(name, OPT_VALUE_THROWN);

	// This assertion is a nice idea, but practically speaking we don't
	// currently have a moment when an error is caught with PUSH_TRAP
	// to set it to trash...only if it has its value processed as a
	// function return or loop break, etc.  One way of fixing it would
	// be to make PUSH_TRAP take 3 arguments instead of 2, and store
	// the error argument in the Rebol_State if it gets thrown...but
	// that looks a bit ugly.  Think more on this.

	/* assert(IS_TRASH(TASK_THROWN_ARG)); */

	*TASK_THROWN_ARG = *arg;
}


/***********************************************************************
**
*/	void Take_Thrown_Arg_Debug(REBVAL *out, REBVAL *thrown)
/*
**		Debug-only version of TAKE_THROWN_ARG
**
**		Gets the task-local value associated with the thrown,
**		and clears the thrown bit from thrown.
**
**		WARNING: 'out' can be the same pointer as 'thrown'
**
***********************************************************************/
{
	assert(THROWN(thrown));
	VAL_CLR_OPT(thrown, OPT_VALUE_THROWN);

	// See notes about assertion in Convert_Name_To_Thrown_Debug.  TBD.

	/* assert(!IS_TRASH(TASK_THROWN_ARG)); */

	*out = *TASK_THROWN_ARG;

	// The THROWN_ARG lives under the root set, and must be a value
	// that won't trip up the GC.
	SET_TRASH_SAFE(TASK_THROWN_ARG);
}


/***********************************************************************
**
*/	ATTRIBUTE_NO_RETURN void Raise_Core(const REBVAL *err)
/*
**		Cause a "trap" of an error by longjmp'ing to the enclosing
**		PUSH_TRAP or PUSH_TRAP_ANY.  Although the error being passed
**		may not be something that strictly represents an error
**		condition (e.g. a BREAK or CONTINUE or THROW), if it gets
**		passed to this routine then it has not been caught by its
**		intended recipient, and is being treated as an error.
**
***********************************************************************/
{
	ASSERT_ERROR(err);

#if !defined(NDEBUG)
	// If we throw the error we'll lose the stack, and if it's an early
	// error we always want to see it (do not use ATTEMPT or TRY on
	// purpose in Init_Core()...)
	if (PG_Boot_Phase < BOOT_DONE) {
		Debug_Fmt("** Error raised during Init_Core(), should not happen!");
		Debug_Fmt("%v", err);
		assert(FALSE);
	}
#endif

	if (!Saved_State) {
		// Print out the error before crashing
		Print_Value(err, 0, FALSE);
		panic Error_0(RE_NO_SAVED_STATE);
	}

	if (Trace_Level) {
		if (THROWN(err)) {
			// !!! Write some kind of error tracer for errors that do not
			// have frames, so you can trace quits/etc.
		} else
			Debug_Fmt(
				cs_cast(BOOT_STR(RS_TRACE, 10)),
				&VAL_ERR_VALUES(err)->type,
				&VAL_ERR_VALUES(err)->id
			);
	}

	// Error may live in a local variable whose stack is going away, or
	// other unstable location.  Copy before the jump.

	Saved_State->error = *err;

	LONG_JUMP(Saved_State->cpu_state, 1);
}


/***********************************************************************
**
*/	void Trap_Stack_Overflow(void)
/*
**		See comments on C_STACK_OVERFLOWING.  This routine is
**		deliberately separate and simple so that it allocates no
**		objects or locals...and doesn't run any code that itself
**		might wind up calling C_STACK_OVERFLOWING.
**
***********************************************************************/
{
	if (!Saved_State) panic Error_0(RE_NO_SAVED_STATE);

	Saved_State->error = *TASK_STACK_ERROR; // pre-allocated

	LONG_JUMP(Saved_State->cpu_state, 1);
}


/***********************************************************************
**
*/	REBCNT Stack_Depth(void)
/*
***********************************************************************/
{
	struct Reb_Call *call = DSF;
	REBCNT count = 0;

	while (call) {
		count++;
		call = PRIOR_DSF(call);
	}

	return count;
}


/***********************************************************************
**
*/	REBSER *Make_Backtrace(REBINT start)
/*
**		Return a block of backtrace words.
**
***********************************************************************/
{
	REBCNT depth = Stack_Depth();
	REBSER *blk = Make_Array(depth - start);
	struct Reb_Call *call;
	REBVAL *val;

	for (call = DSF; call != NULL; call = PRIOR_DSF(call)) {
		if (start-- <= 0) {
			val = Alloc_Tail_Array(blk);
			Val_Init_Word_Unbound(val, REB_WORD, VAL_WORD_SYM(DSF_LABEL(call)));
		}
	}

	return blk;
}


/***********************************************************************
**
*/	void Set_Error_Type(ERROR_OBJ *error)
/*
**		Sets error type and id fields based on code number.
**
***********************************************************************/
{
	REBSER *cats;		// Error catalog object
	REBSER *cat;		// Error category object
	REBCNT n;		// Word symbol number
	REBINT code;

	code = VAL_INT32(&error->code);

	// Set error category:
	n = code / 100 + 1;
	cats = VAL_OBJ_FRAME(Get_System(SYS_CATALOG, CAT_ERRORS));

	if (code >= 0 && n < SERIES_TAIL(cats) &&
		(cat = VAL_ERR_OBJECT(BLK_SKIP(cats, n)))
	) {
		Val_Init_Word(&error->type, REB_WORD, FRM_KEY_SYM(cats, n), cats, n);

		// Find word related to the error itself:

		n = code % 100 + 3;
		if (n < SERIES_TAIL(cat))
			Val_Init_Word(&error->id, REB_WORD, FRM_KEY_SYM(cat, n), cat, n);
	}
}


/***********************************************************************
**
*/	REBVAL *Find_Error_Info(ERROR_OBJ *error, REBINT *num)
/*
**		Return the error message needed to print an error.
**		Must scan the error catalog and its error lists.
**		Note that the error type and id words no longer need
**		to be bound to the error catalog context.
**		If the message is not found, return null.
**
***********************************************************************/
{
	REBSER *frame;
	REBVAL *obj1;
	REBVAL *obj2;

	if (!IS_WORD(&error->type) || !IS_WORD(&error->id)) return 0;

	// Find the correct error type object in the catalog:
	frame = VAL_OBJ_FRAME(Get_System(SYS_CATALOG, CAT_ERRORS));
	obj1 = Find_Word_Value(frame, VAL_WORD_SYM(&error->type));
	if (!obj1) return 0;

	// Now find the correct error message for that type:
	frame = VAL_OBJ_FRAME(obj1);
	obj2 = Find_Word_Value(frame, VAL_WORD_SYM(&error->id));
	if (!obj2) return 0;

	if (num) {
		obj1 = Find_Word_Value(frame, SYM_CODE);
		if (!obj1) return 0;
		*num = VAL_INT32(obj1)
			+ Find_Word_Index(frame, VAL_WORD_SYM(&error->id), FALSE)
			- Find_Word_Index(frame, SYM_TYPE, FALSE) - 1;
	}

	return obj2;
}


/***********************************************************************
**
*/	void Val_Init_Error(REBVAL *out, REBSER *err_frame)
/*
**		Returns FALSE if a THROWN() value is made during evaluation.
**
***********************************************************************/
{
	ENSURE_FRAME_MANAGED(err_frame);

	VAL_SET(out, REB_ERROR);
	VAL_ERR_OBJECT(out) = err_frame;

	ASSERT_ERROR(out);
}


/***********************************************************************
**
*/	REBOOL Make_Error_Object_Throws(REBVAL *out, REBVAL *arg)
/*
**		Creates an error object from arg and puts it in value.
**		The arg can be a string or an object body block.
**
**		Returns TRUE if a THROWN() value is made during evaluation.
**
**		This function is called by MAKE ERROR!.
**
***********************************************************************/
{
	REBSER *err;		// Error object
	ERROR_OBJ *error;	// Error object values
	REBINT code = 0;

	// Create a new error object from another object, including any non-standard fields:
	if (IS_ERROR(arg) || IS_OBJECT(arg)) {
		err = Merge_Frames(VAL_OBJ_FRAME(ROOT_ERROBJ),
			IS_ERROR(arg) ? VAL_ERR_OBJECT(arg) : VAL_OBJ_FRAME(arg));
		error = ERR_VALUES(err);

		if (!Find_Error_Info(error, &code)) code = RE_INVALID_ERROR;
		SET_INTEGER(&error->code, code);

		Val_Init_Error(out, err);
		return FALSE;
	}

	// Make a copy of the error object template:
	err = Copy_Array_Shallow(VAL_OBJ_FRAME(ROOT_ERROBJ));

	error = ERR_VALUES(err);
	SET_NONE(&error->id);

	// If block arg, evaluate object values (checking done later):
	// If user set error code, use it to setup type and id fields.
	if (IS_BLOCK(arg)) {
		REBVAL evaluated;

		// Bind and do an evaluation step (as with MAKE OBJECT! with A_MAKE
		// code in REBTYPE(Object) and code in REBNATIVE(construct))
		Bind_Values_Deep(VAL_BLK_DATA(arg), err);
		if (Do_Block_Throws(&evaluated, VAL_SERIES(arg), 0)) {
			*out = evaluated;
			return TRUE;
		}

		if (IS_INTEGER(&error->code) && VAL_INT64(&error->code)) {
			Set_Error_Type(error);
		} else {
			if (Find_Error_Info(error, &code)) {
				SET_INTEGER(&error->code, code);
			}
		}
		// The error code is not valid:
		if (IS_NONE(&error->id)) {
			SET_INTEGER(&error->code, RE_INVALID_ERROR);
			Set_Error_Type(error);
		}
		if (
			VAL_INT64(&error->code) < RE_SPECIAL_MAX
			|| VAL_INT64(&error->code) >= RE_MAX
		) {
			Free_Series(err);
			raise Error_Invalid_Arg(arg);
		}
	}

	// If string arg, setup other fields
	else if (IS_STRING(arg)) {
		SET_INTEGER(&error->code, RE_USER); // user error
		Val_Init_String(&error->arg1, Copy_Sequence_At_Position(arg));
		Set_Error_Type(error);
	}
	else
		raise Error_Invalid_Arg(arg);

	MANAGE_SERIES(err);
	Val_Init_Error(out, err);

	return FALSE;
}


/***********************************************************************
**
*/	REBSER *Make_Error_Core(REBINT code, const char *c_file, int c_line, va_list *args)
/*
**		(va_list by pointer: http://stackoverflow.com/a/3369762/211160)
**
**		Create and init a new error object.
**
***********************************************************************/
{
	REBSER *err;		// Error object
	ERROR_OBJ *error;	// Error object values
	REBVAL *arg;

	assert(code != 0);

	if (PG_Boot_Phase < BOOT_ERRORS) {
		Panic_Core(code, NULL, c_file, c_line, args);
		DEAD_END;
	}

	// Make a copy of the error object template's frame  Note that by shallow
	// copying it we are implicitly reusing the original's word series..
	// which has already been indicated as "Managed".  We set our copy to
	// managed so that it matches.
	err = Copy_Array_Shallow(VAL_OBJ_FRAME(ROOT_ERROBJ));
	MANAGE_SERIES(err);

	error = ERR_VALUES(err);

	// Set error number:
	SET_INTEGER(&error->code, code);
	Set_Error_Type(error);

	// Set error argument values:
	arg = va_arg(*args, REBVAL*);

	if (arg) {
		error->arg1 = *arg;
		arg = va_arg(*args, REBVAL*);
	}

	if (arg) {
		error->arg2 = *arg;
		arg = va_arg(*args, REBVAL*);
	}

	if (arg) {
		error->arg3 = *arg;
		arg = va_arg(*args, REBVAL*);
	}

#if !defined(NDEBUG)
	if (arg) {
		// Implementation previously didn't take a vararg, and so was
		// limited to 3 arguments.  Could be generalized more now

		Debug_Fmt("Make_Error() passed more than 3 error arguments!");
		panic Error_0(RE_MISC);
	}

	assert(c_file);

	{
		// !!! We have the C source file and line information for where the
		// error was triggered (since Make_Error_Core calls all originate
		// from C source, as opposed to the user path where the error
		// is made in the T_Object dispatch).  However, the error object
		// template is defined in sysobj.r, and there's no way to add
		// "debug only" fields to it.  We create REBVALs for the file and
		// line just to show they are available here, if there were some
		// good way to put them into the object (perhaps they should be
		// associated via a map or list, and not put inside?)
		//
		REBVAL c_file_value;
		REBVAL c_line_value;
		Val_Init_File(
			&c_file_value,
			Append_UTF8(NULL, cb_cast(c_file), LEN_BYTES(cb_cast(c_file)))
		);
		SET_INTEGER(&c_line_value, c_line);
	}
#endif

	// Set backtrace and location information:
	if (DSF) {
		// Where (what function) is the error:
		Val_Init_Block(&error->where, Make_Backtrace(0));
		// Nearby location of the error (in block being evaluated):
		error->nearest = *DSF_WHERE(DSF);
	}

	return err;
}


/***********************************************************************
**
*/	REBSER *Make_Error(REBCNT num, ...)
/*
***********************************************************************/
{
	va_list args;
	REBSER *error;

	va_start(args, num);

#ifdef NDEBUG
	error = Make_Error_Core(num, NULL, 0, &args);
#else
	error = Make_Error_Core(num, __FILE__, __LINE__, &args);
#endif

	va_end(args);

	return error;
}


/***********************************************************************
**
*/	ATTRIBUTE_NO_RETURN void Error_Null(REBCNT num, ... /* REBVAL *arg1, REBVAL *arg2, ... NULL */)
/*
**		This is a variadic function which is designed to be the
**		"argument" of either a `raise` or a `panic` "keyword".
**		It can be called directly, or indirectly by another proxy
**		error function.  It takes a number of REBVAL* arguments
**		appropriate for the error number passed.
**
**		Although it is made to look like an argument to an action,
**		this function actually does the Raising or Panic'ing.  The
**		macro keywords only set which failure type to put in effect,
**		and in debug builds that macro also captures the file and
**		line number at the point of invocation.  This routine then
**		reads those global values.
**
**		If no `raise` or `panic` was in effect, Error() will assert
**		regarding the missing instruction.
**
**		(See complete explanation in notes on `raise` and `panic`)
**
***********************************************************************/
{
	va_list args;

	switch (TG_Fail_Prep) {
		// Default ok anywhere: http://stackoverflow.com/questions/3110088/
		default:
			Debug_Fmt("UNKNOWN TG_Fail_Prep VALUE: %d", TG_Fail_Prep);
			assert(FALSE);
			/* fallthrough */ // line can't be blank for Coverity
		case FAIL_UNPREPARED:
			Debug_Fmt("FAIL_UNPREPARED in Error()");
			assert(FALSE);
			/* fallthrough */ // line can't be blank for Coverity
		case FAIL_PREP_PANIC:
			va_start(args, num);
			Panic_Core(num, NULL, TG_Fail_C_File, TG_Fail_C_Line, &args);
			// crashed!

		case FAIL_PREP_RAISE: {
			REBVAL error;

			// Clear fail prep flag so `raise` status doesn't linger
			// (also, Make_Error_Core may Panic--we might assert no prep)
			TG_Fail_Prep = FAIL_UNPREPARED;

			va_start(args, num);
			Val_Init_Error(
				&error,
				Make_Error_Core(num, TG_Fail_C_File, TG_Fail_C_Line, &args)
			);
			va_end(args);

			Raise_Core(&error);
			// longjmp'd!
		}
	}

	DEAD_END;
}


#if !defined(NDEBUG)

/***********************************************************************
**
*/	ATTRIBUTE_NO_RETURN void Error_0_Debug(REBCNT num)
/*
**		Debug-only version of Error_0 macro that checks arg types.
**
***********************************************************************/
{
	Error_Null(num, NULL);
}


/***********************************************************************
**
*/	ATTRIBUTE_NO_RETURN void Error_1_Debug(REBCNT num, const REBVAL *arg1)
/*
**		Debug-only version of Error_1 macro that checks arg types.
**
***********************************************************************/
{
	Error_Null(num, arg1, NULL);
}


/***********************************************************************
**
*/	ATTRIBUTE_NO_RETURN void Error_2_Debug(REBCNT num, const REBVAL *arg1, const REBVAL *arg2)
/*
**		Debug-only version of Error_2 macro that checks arg types.
**
***********************************************************************/
{
	Error_Null(num, arg1, arg2, NULL);
}


/***********************************************************************
**
*/	ATTRIBUTE_NO_RETURN void Error_3_Debug(REBCNT num, const REBVAL *arg1, const REBVAL *arg2, const REBVAL *arg3)
/*
**		Debug-only version of Error_3 macro that checks arg types.
**
***********************************************************************/
{
	Error_Null(num, arg1, arg2, arg3, NULL);
}

#endif


/***********************************************************************
**
*/	ATTRIBUTE_NO_RETURN void Error_No_Arg(const REBVAL *label, const REBVAL *key)
/*
***********************************************************************/
{
	REBVAL key_word;
	assert(IS_TYPESET(key));
	Val_Init_Word_Unbound(&key_word, REB_WORD, VAL_TYPESET_SYM(key));
	Error_2(RE_NO_ARG, label, &key_word);
}


/***********************************************************************
**
*/	ATTRIBUTE_NO_RETURN void Error_Invalid_Datatype(REBCNT id)
/*
***********************************************************************/
{
	REBVAL id_value;
	SET_INTEGER(&id_value, id);
	Error_1(RE_INVALID_DATATYPE, &id_value);
}


/***********************************************************************
**
*/	ATTRIBUTE_NO_RETURN void Error_No_Memory(REBCNT bytes)
/*
***********************************************************************/
{
	REBVAL bytes_value;
	SET_INTEGER(&bytes_value, bytes);
	Error_1(RE_NO_MEMORY, &bytes_value);
}


/***********************************************************************
**
*/	ATTRIBUTE_NO_RETURN void Error_Invalid_Arg(const REBVAL *value)
/*
**		This error is pretty vague...it's just "invalid argument"
**		and the value with no further commentary or context.  It
**		becomes a catch all for "unexpected input" when a more
**		specific error would be more useful.
**
***********************************************************************/
{
	Error_1(RE_INVALID_ARG, value);
}


/***********************************************************************
**
*/	ATTRIBUTE_NO_RETURN void Error_No_Catch_For_Throw(REBVAL *thrown)
/*
***********************************************************************/
{
	REBVAL arg;
	assert(THROWN(thrown));
	TAKE_THROWN_ARG(&arg, thrown); // clears bit

	if (IS_NONE(thrown))
		Error_1(RE_NO_CATCH, &arg);
	else
		Error_2(RE_NO_CATCH_NAMED, &arg, thrown);
}


/***********************************************************************
**
*/	ATTRIBUTE_NO_RETURN void Error_Has_Bad_Type(const REBVAL *value)
/*
**		<type> type is not allowed here
**
***********************************************************************/
{
	Error_1(RE_INVALID_TYPE, Type_Of(value));
}


/***********************************************************************
**
*/	ATTRIBUTE_NO_RETURN void Error_Out_Of_Range(const REBVAL *arg)
/*
**		value out of range: <value>
**
***********************************************************************/
{
	Error_1(RE_OUT_OF_RANGE, arg);
}


/***********************************************************************
**
*/	ATTRIBUTE_NO_RETURN void Error_Protected_Key(REBVAL *key)
/*
***********************************************************************/
{
	REBVAL key_name;
	assert(IS_TYPESET(key));
	Val_Init_Word_Unbound(&key_name, REB_WORD, VAL_TYPESET_SYM(key));

	Error_1(RE_LOCKED_WORD, &key_name);
}


/***********************************************************************
**
*/	ATTRIBUTE_NO_RETURN void Error_Illegal_Action(REBCNT type, REBCNT action)
/*
***********************************************************************/
{
	REBVAL action_word;
	Val_Init_Word_Unbound(&action_word, REB_WORD, Get_Action_Sym(action));

	Error_2(RE_CANNOT_USE, &action_word, Get_Type(type));
}


/***********************************************************************
**
*/	ATTRIBUTE_NO_RETURN void Error_Math_Args(enum Reb_Kind type, REBCNT action)
/*
***********************************************************************/
{
	REBVAL action_word;
	Val_Init_Word_Unbound(&action_word, REB_WORD, Get_Action_Sym(action));

	Error_2(RE_NOT_RELATED, &action_word, Get_Type(type));
}


/***********************************************************************
**
*/	ATTRIBUTE_NO_RETURN void Error_Unexpected_Type(enum Reb_Kind expected, enum Reb_Kind actual)
/*
***********************************************************************/
{
	assert(expected != REB_END && expected < REB_MAX);
	assert(actual != REB_END && actual < REB_MAX);

	// if (type2 == REB_END) Error_1(errnum, Get_Type(type1));
	raise Error_2(RE_EXPECT_VAL, Get_Type(expected), Get_Type(actual));
}


/***********************************************************************
**
*/	ATTRIBUTE_NO_RETURN void Error_Arg_Type(const struct Reb_Call *call, const REBVAL *param, const REBVAL *arg_type)
/*
**		Function in frame of `call` expected parameter `param` to be
**		a type different than the arg given (which had `arg_type`)
**
***********************************************************************/
{
	REBVAL param_word;
	assert(IS_TYPESET(param));
	Val_Init_Word_Unbound(&param_word, REB_WORD, VAL_TYPESET_SYM(param));

	assert(IS_DATATYPE(arg_type));
	Error_3(RE_EXPECT_ARG, DSF_LABEL(call), &param_word, arg_type);
}


/***********************************************************************
**
*/	ATTRIBUTE_NO_RETURN void Error_Bad_Make(REBCNT type, const REBVAL *spec)
/*
***********************************************************************/
{
	Error_2(RE_BAD_MAKE_ARG, Get_Type(type), spec);
}


/***********************************************************************
**
*/	ATTRIBUTE_NO_RETURN void Error_Cannot_Reflect(REBCNT type, const REBVAL *arg)
/*
***********************************************************************/
{
	Error_2(RE_CANNOT_USE, arg, Get_Type(type));
}


/***********************************************************************
**
*/	ATTRIBUTE_NO_RETURN void Error_On_Port(REBCNT errnum, REBSER *port, REBINT err_code)
/*
***********************************************************************/
{
	REBVAL *spec = OFV(port, STD_PORT_SPEC);
	REBVAL *val;
	REBVAL err_code_value;

	if (!IS_OBJECT(spec)) raise Error_0(RE_INVALID_PORT);

	val = Get_Object(spec, STD_PORT_SPEC_HEAD_REF); // most informative
	if (IS_NONE(val)) val = Get_Object(spec, STD_PORT_SPEC_HEAD_TITLE);

	SET_INTEGER(&err_code_value, err_code);
	Error_2(errnum, val, &err_code_value);
}


/***********************************************************************
**
*/	int Exit_Status_From_Value(REBVAL *value)
/*
**		This routine's job is to turn an arbitrary value into an
**		operating system exit status:
**
**			https://en.wikipedia.org/wiki/Exit_status
**
***********************************************************************/
{
	assert(!THROWN(value));

	if (IS_INTEGER(value)) {
		// Fairly obviously, an integer should return an integer
		// result.  But Rebol integers are 64 bit and signed, while
		// exit statuses don't go that large.
		//
		return VAL_INT32(value);
	}
	else if (IS_UNSET(value) || IS_NONE(value)) {
		// An unset would happen with just QUIT or EXIT and no /WITH,
		// so treating that as a 0 for success makes sense.  A NONE!
		// seems like nothing to report as well, for instance:
		//
		//     exit/with if badthing [badthing-code]
		//
		return 0;
	}
	else if (IS_ERROR(value)) {
		// Rebol errors do have an error number in them, and if your
		// program tries to return a Rebol error it seems it wouldn't
		// hurt to try using that.  They may be out of range for
		// platforms using byte-sized error codes, however...but if
		// that causes bad things OS_EXIT() should be graceful about it.
		//
		return VAL_ERR_NUM(value);
	}

	// Just 1 otherwise.
	//
	return 1;
}


/***********************************************************************
**
*/	void Init_Errors(REBVAL *errors)
/*
***********************************************************************/
{
	REBSER *errs;
	REBVAL *val;

	// Create error objects and error type objects:
	*ROOT_ERROBJ = *Get_System(SYS_STANDARD, STD_ERROR);
	errs = Construct_Object(NULL, VAL_BLK_HEAD(errors), FALSE);

	Val_Init_Object(Get_System(SYS_CATALOG, CAT_ERRORS), errs);

	// Create objects for all error types:
	for (val = BLK_SKIP(errs, 1); NOT_END(val); val++) {
		errs = Construct_Object(NULL, VAL_BLK_HEAD(val), FALSE);
		Val_Init_Object(val, errs);
	}
}


/***********************************************************************
**
*/	REBYTE *Security_Policy(REBCNT sym, REBVAL *name)
/*
**	Given a security symbol (like FILE) and a value (like the file
**	path) returns the security policy (RWX) allowed for it.
**
**	Args:
**
**		sym:  word that represents the type ['file 'net]
**		name: file or path value
**
**	Returns BTYE array of flags for the policy class:
**
**		flags: [rrrr wwww xxxx ----]
**
**		Where each byte is:
**			0: SEC_ALLOW
**			1: SEC_ASK
**			2: SEC_THROW
**			3: SEC_QUIT
**
**	The secuity is defined by the system/state/policies object, that
**	is of the form:
**
**		[
**			file:  [%file1 tuple-flags %file2 ... default tuple-flags]
**			net:   [...]
**			call:  tuple-flags
**			stack: tuple-flags
**			eval:  integer (limit)
**		]
**
***********************************************************************/
{
	REBVAL *policy = Get_System(SYS_STATE, STATE_POLICIES);
	REBYTE *flags;
	REBCNT len;
	REBCNT errcode = RE_SECURITY_ERROR;

	if (!IS_OBJECT(policy)) goto error;

	// Find the security class in the block: (file net call...)
	policy = Find_Word_Value(VAL_OBJ_FRAME(policy), sym);
	if (!policy) goto error;

	// Obtain the policies for it:
	// Check for a master tuple: [file rrrr.wwww.xxxx]
	if (IS_TUPLE(policy)) return VAL_TUPLE(policy); // non-aligned
	// removed A90: if (IS_INTEGER(policy)) return (REBYTE*)VAL_INT64(policy); // probably not used

	// Only other form is detailed block:
	if (!IS_BLOCK(policy)) goto error;

	// Scan block of policies for the class: [file [allow read quit write]]
	len = 0;	// file or url length
	flags = 0;	// policy flags
	for (policy = VAL_BLK_HEAD(policy); NOT_END(policy); policy += 2) {

		// Must be a policy tuple:
		if (!IS_TUPLE(policy+1)) goto error;

		// Is it a policy word:
		if (IS_WORD(policy)) { // any word works here
			// If no strings found, use the default:
			if (len == 0) flags = VAL_TUPLE(policy+1); // non-aligned
		}

		// Is it a string (file or URL):
		else if (ANY_BINSTR(policy) && name) {
			//Debug_Fmt("sec: %r %r", policy, name);
			if (Match_Sub_Path(VAL_SERIES(policy), VAL_SERIES(name))) {
				// Is the match adequate?
				if (VAL_TAIL(name) >= len) {
					len = VAL_TAIL(name);
					flags = VAL_TUPLE(policy+1); // non-aligned
				}
			}
		}
		else goto error;
	}

	if (!flags) {
		errcode = RE_SECURITY;
		policy = name ? name : 0;
error:
		if (!policy) {
			Val_Init_Word_Unbound(DS_TOP, REB_WORD, sym);
			policy = DS_TOP;
		}
		raise Error_1(errcode, policy);
	}

	return flags;
}


/***********************************************************************
**
*/	void Trap_Security(REBCNT flag, REBCNT sym, REBVAL *value)
/*
**		Take action on the policy flags provided. The sym and value
**		are provided for error message purposes only.
**
***********************************************************************/
{
	if (flag == SEC_THROW) {
		if (!value) {
			Val_Init_Word_Unbound(DS_TOP, REB_WORD, sym);
			value = DS_TOP;
		}
		raise Error_1(RE_SECURITY, value);
	}
	else if (flag == SEC_QUIT) OS_EXIT(101);
}


/***********************************************************************
**
*/	void Check_Security(REBCNT sym, REBCNT policy, REBVAL *value)
/*
**		A helper function that fetches the security flags for
**		a given symbol (FILE) and value (path), and then tests
**		that they are allowed.
**
***********************************************************************/
{
	REBYTE *flags;

	flags = Security_Policy(sym, value);
	Trap_Security(flags[policy], sym, value);
}


#if !defined(NDEBUG)

/***********************************************************************
**
*/	void Assert_Error_Debug(const REBVAL *err)
/*
**		Debug-only implementation of ASSERT_ERROR
**
***********************************************************************/
{
	assert(IS_ERROR(err));
	assert(VAL_ERR_NUM(err) != 0);

	ASSERT_FRAME(VAL_ERR_OBJECT(err));
}

#endif
