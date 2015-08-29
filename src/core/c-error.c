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

	s->hold_tail = GC_Protect->tail;
	s->gc_disable = GC_Disabled;

#if !defined(NDEBUG)
	s->manuals = GC_Manuals;
#endif

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

	GC_Protect->tail = state->hold_tail;

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
*/	void Do_Error(const REBVAL *err)
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
		Panic(RP_NO_SAVED_STATE); // or RP_NO_CATCH ?
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
**		See comments on CHECK_C_STACK_OVERFLOW.  This routine is
**		deliberately separate and simple so that it allocates no
**		objects or locals...and doesn't run any code that itself
**		might wind up calling CHECK_C_STACK_OVERFLOW.
**
***********************************************************************/
{
	if (!Saved_State) Panic(RP_NO_SAVED_STATE);

	Saved_State->error = *TASK_STACK_ERROR; // pre-allocated

	LONG_JUMP(Saved_State->cpu_state, 1);
}



/***********************************************************************
**
*/	void Halt(void)
/*
**		Halts are designed to go all the way up to the top level of
**		the CATCH stack.  They cannot be intercepted by any
**		intermediate stack levels.
**
***********************************************************************/
{
	Do_Error(TASK_HALT_ERROR);
}


/***********************************************************************
**
*/	REBCNT Stack_Depth(void)
/*
***********************************************************************/
{
	struct Reb_Call *call = DSF;
	REBCNT count = 0;

	for (call = DSF; call != NULL; call = PRIOR_DSF(call)) {
		count++;
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
		Val_Init_Word(&error->type, REB_WORD, FRM_WORD_SYM(cats, n), cats, n);

		// Find word related to the error itself:

		n = code % 100 + 3;
		if (n < SERIES_TAIL(cat))
			Val_Init_Word(&error->id, REB_WORD, FRM_WORD_SYM(cat, n), cat, n);
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
	VAL_ERR_NUM(out) = VAL_INT32(&ERR_VALUES(err_frame)->code);
	VAL_ERR_OBJECT(out) = err_frame;

	ASSERT_ERROR(out);
}


/***********************************************************************
**
*/	REBOOL Make_Error_Object(REBVAL *out, REBVAL *arg)
/*
**		Creates an error object from arg and puts it in value.
**		The arg can be a string or an object body block.
**		This function is called by MAKE ERROR!.
**
**		Returns FALSE if a THROWN() value is made during evaluation.
**
***********************************************************************/
{
	REBSER *err;		// Error object
	ERROR_OBJ *error;	// Error object values
	REBINT code = 0;

	// Create a new error object from another object, including any non-standard fields:
	if (IS_ERROR(arg) || IS_OBJECT(arg)) {
		err = Merge_Frames(VAL_OBJ_FRAME(ROOT_ERROBJ),
			IS_ERROR(arg) ? VAL_OBJ_FRAME(arg) : VAL_ERR_OBJECT(arg));
		error = ERR_VALUES(err);

		if (!Find_Error_Info(error, &code)) code = RE_INVALID_ERROR;
		SET_INTEGER(&error->code, code);

		Val_Init_Error(out, err);
		return TRUE;
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
			return FALSE;
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
		if (VAL_INT64(&error->code) < 100 || VAL_INT64(&error->code) > 1000)
			Trap_Arg(arg);
	}

	// If string arg, setup other fields
	else if (IS_STRING(arg)) {
		SET_INTEGER(&error->code, RE_USER); // user error
		Val_Init_String(&error->arg1, Copy_Sequence_At_Position(arg));
		Set_Error_Type(error);
	}
	else
		Trap_Arg(arg);

	MANAGE_SERIES(err);
	Val_Init_Error(out, err);

	return TRUE;
}


/***********************************************************************
**
*/	REBSER *Make_Error(REBINT code, const REBVAL *arg1, const REBVAL *arg2, const REBVAL *arg3)
/*
**		Create and init a new error object.
**
***********************************************************************/
{
	REBSER *err;		// Error object
	ERROR_OBJ *error;	// Error object values

	assert(code != 0);

	if (PG_Boot_Phase < BOOT_ERRORS) {
		assert(FALSE);
		Panic_Core(RP_EARLY_ERROR, code); // Not far enough!
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
	if (arg1) error->arg1 = *arg1;
	if (arg2) error->arg2 = *arg2;
	if (arg3) error->arg3 = *arg3;

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
*/	void Trap(REBCNT num)
/*
***********************************************************************/
{
	REBVAL error;
	Val_Init_Error(&error, Make_Error(num, 0, 0, 0));
	Do_Error(&error);
}


/***********************************************************************
**
*/	void Trap1(REBCNT num, const REBVAL *arg1)
/*
***********************************************************************/
{
	REBVAL error;
	Val_Init_Error(&error, Make_Error(num, arg1, 0, 0));
	Do_Error(&error);
}


/***********************************************************************
**
*/	void Trap2(REBCNT num, const REBVAL *arg1, const REBVAL *arg2)
/*
***********************************************************************/
{
	REBVAL error;
	Val_Init_Error(&error, Make_Error(num, arg1, arg2, 0));
	Do_Error(&error);
}


/***********************************************************************
**
*/	void Trap3(REBCNT num, const REBVAL *arg1, const REBVAL *arg2, const REBVAL *arg3)
/*
***********************************************************************/
{
	REBVAL error;
	Val_Init_Error(&error, Make_Error(num, arg1, arg2, arg3));
	Do_Error(&error);
}


/***********************************************************************
**
*/	void Trap_Arg(const REBVAL *arg)
/*
***********************************************************************/
{
	Trap1(RE_INVALID_ARG, arg);
}


/***********************************************************************
**
*/	void Trap_Thrown(REBVAL *thrown)
/*
***********************************************************************/
{
	REBVAL arg;
	assert(THROWN(thrown));
	TAKE_THROWN_ARG(&arg, thrown); // clears bit

	if (IS_NONE(thrown))
		Trap1(RE_NO_CATCH, &arg);
	else
		Trap2(RE_NO_CATCH_NAMED, &arg, thrown);

	DEAD_END_VOID;
}


/***********************************************************************
**
*/	void Trap_Type(const REBVAL *arg)
/*
**		<type> type is not allowed here
**
***********************************************************************/
{
	Trap1(RE_INVALID_TYPE, Of_Type(arg));
}


/***********************************************************************
**
*/	void Trap_Range(const REBVAL *arg)
/*
**		value out of range: <value>
**
***********************************************************************/
{
	Trap1(RE_OUT_OF_RANGE, arg);
}


/***********************************************************************
**
*/	void Trap_Word(REBCNT num, REBCNT sym, const REBVAL *arg)
/*
***********************************************************************/
{
	Val_Init_Word_Unbound(DS_TOP, REB_WORD, sym);
	if (arg) Trap2(num, DS_TOP, arg);
	else Trap1(num, DS_TOP);
}


/***********************************************************************
**
*/	void Trap_Action(REBCNT type, REBCNT action)
/*
***********************************************************************/
{
	Trap2(RE_CANNOT_USE, Get_Action_Word(action), Get_Type(type));
}


/***********************************************************************
**
*/	void Trap_Math_Args(REBCNT type, REBCNT action)
/*
***********************************************************************/
{
	Trap2(RE_NOT_RELATED, Get_Action_Word(action), Get_Type(type));
}


/***********************************************************************
**
*/	void Trap_Types(REBCNT errnum, REBCNT type1, REBCNT type2)
/*
***********************************************************************/
{
	if (type2 != 0) Trap2(errnum, Get_Type(type1), Get_Type(type2));
	Trap1(errnum, Get_Type(type1));
}


/***********************************************************************
**
*/	void Trap_Expect(const REBVAL *object, REBCNT index, REBCNT type)
/*
**		Object field is not of expected type.
**		PORT expected SCHEME of OBJECT type
**
***********************************************************************/
{
	Trap3(RE_EXPECT_TYPE, Of_Type(object), Obj_Word(object, index), Get_Type(type));
}


/***********************************************************************
**
*/	void Trap_Make(REBCNT type, const REBVAL *spec)
/*
***********************************************************************/
{
	Trap2(RE_BAD_MAKE_ARG, Get_Type(type), spec);
}


/***********************************************************************
**
*/	void Trap_Num(REBCNT err, REBCNT num)
/*
***********************************************************************/
{
	DS_PUSH_INTEGER(num);
	Trap1(err, DS_TOP);
}


/***********************************************************************
**
*/	void Trap_Reflect(REBCNT type, const REBVAL *arg)
/*
***********************************************************************/
{
	Trap2(RE_CANNOT_USE, arg, Get_Type(type));
}


/***********************************************************************
**
*/	void Trap_Port(REBCNT errnum, REBSER *port, REBINT err_code)
/*
***********************************************************************/
{
	REBVAL *spec = OFV(port, STD_PORT_SPEC);
	REBVAL *val;

	if (!IS_OBJECT(spec)) Trap(RE_INVALID_PORT);

	val = Get_Object(spec, STD_PORT_SPEC_HEAD_REF); // most informative
	if (IS_NONE(val)) val = Get_Object(spec, STD_PORT_SPEC_HEAD_TITLE);

	DS_PUSH_INTEGER(err_code);
	Trap2(errnum, val, DS_TOP);
}


/***********************************************************************
**
*/	REBINT Process_Loop_Throw(REBVAL *val)
/*
**		Process values thrown during loop. Returns:
**
**			 1 - break or break/return (changes result)
**			-1 - if continue, change val to unset
**			 0 - if not break or continue
**			else: error if not an ERROR value
**
***********************************************************************/
{
	assert(THROWN(val));

	// Using words for starters to parallel VAL_ERR_SYM()
	if (!IS_WORD(val))
		return 0;

	// If it's a BREAK, get the /WITH value (UNSET! if no /WITH):
	if (VAL_WORD_SYM(val) == SYM_BREAK) {
		TAKE_THROWN_ARG(val, val);
		return 1;
	}

	// If it's a CONTINUE then wipe out the
	if (VAL_WORD_SYM(val) == SYM_CONTINUE) {
		SET_UNSET(val);
		return -1;
	}

	// Else: Let all other thrown values bubble up
	return 0;
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
		Trap1_DEAD_END(errcode, policy);
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
		Trap1(RE_SECURITY, value);
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
