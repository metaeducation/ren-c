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
/*
		The Trap() functions are used for errors within the C code.

		TrapN() provides simple trampoline to the var-arg Make_Error()
		that constructs a new error object.

		The Make_Error function uses the error category objects to
		convert from an error code (integer) to error words and strings.
		Other important state information such as location of error
		and function context are also saved at this point.

		Throw is called to throw the error back to a prior catch.
		A catch is defined using a set of C-macros. During the throw
		the error object is stored in a global: This_Error (because we
		cannot be sure that the longjmp value is able to hold a pointer
		on 64bit CPUs.)

		On the catch side, the Catch_Error function takes the error
		object and stores it into the value provided (normally on the
		DStack).

		Catch_Error can be extended to provide a debugging breakpoint
		for examining the call trace and context frames on the stack.
*/
/*

	Error Handling

	Errors occur in two places:

		1. evaluation of natives and actions
		2. evaluation of a block

	When an error occurs, an error object is built and thrown back to
	the nearest prior catch function. The catch is a longjmp that was
	set by a TRY or similar native.  At that point the interpreter stack
	can be either examined (for debugging) or restored to the current
	catch state.

	The error is returned from the catch as a disarmed error object. At
	that point, the error can be passed around and accessed as a normal
	object (although its datatype is ERROR!). The DISARM function
	becomes unnecessary and will simply copy the fields to a normal
	OBJECT! type.

	Using the new CAUSE native with the error object will re-activate
	the error and throw the error back further to the prior catch.

	The error object will include a new TRACE field that provides a back
	trace of the interpreter stack. This is a block of block pointers
	and may be clipped at some reasonable size (perhaps 10).

	When C code hits an error condition, it calls Trap(id, arg1, arg2, ...).
	This function takes a variable number of arguments.

	BREAK and RETURN

	TRY/RECOVER/EXCEPT.

		try [block]
		try/recover [block] [block]

	TRACE f1, :path/f1, or [f1 f2 f3]
	foo: func [[trace] ...]

*/

// A NOTE ABOUT "CLOBBERING" WARNINGS
//
// With compiler warnings on, it can tell us that values are set
// before the setjmp and then changed before a potential longjmp.
// Were we to try and use index in the catch body, it would be
// undefined.  In this case we don't use it, but for technical
// reasons GCC can't judge this case:
//
//     http://stackoverflow.com/q/7721854/211160
//
// Because of this longjmp/setjmp "clobbering", it's a useful warning to
// have enabled in.  One option for suppressing it would be to mark
// a parameter as 'volatile', but that is implementation-defined.
// It is simpler to use a new variable.

#include "sys-core.h"


/***********************************************************************
**
*/	void Check_Stack(void)
/*
***********************************************************************/
{
	if ((DSP + 100) > (REBINT)SERIES_REST(DS_Series))
		Trap(RE_STACK_OVERFLOW);
}


/***********************************************************************
**
*/	void Push_Catch_Helper(REBOL_STATE *s)
/*
**		Used by both CATCH and CATCH_ANY, whose differentiation comes
**		from how they react to the catch vs. from the initial push.
**
***********************************************************************/
{
	assert(Saved_State || ((DSP == -1) && (DSF == DSF_NONE)));

	s->dsp = DSP;
	s->dsf = DSF;

	s->hold_tail = GC_Protect->tail;
	s->gc_disable = GC_Disabled;

	s->last_state = Saved_State;
	Saved_State = s;

	// !!! garbage collector should probably walk Saved_State stack to
	// keep the error values alive from GC, so use a "safe" trash.
	SET_TRASH_SAFE(&s->error);
}


/***********************************************************************
**
*/	void Drop_Catch_Helper(REBOL_STATE *s, REBOOL caught)
/*
**		Pops a CATCH state from the list without changing the actual
**		state of the data stack or data stack frame.
**
***********************************************************************/
{
	// leave DSP as-is
	// leave DSF as-is

	// !!! Should anything be done with GC_Protect->tail, s->hold_tail ?

	assert(GC_Disabled == s->gc_disable);

	assert(caught ? !IS_TRASH(&s->error) : IS_TRASH(&s->error));

	Saved_State = s->last_state;
}


/***********************************************************************
**
*/	REBOOL Catch_Any_Error_Helper(REBOL_STATE *state)
/*
**		This is the core handler which is used by Catch_Error_Helper
**		as well.  It is necessary for the /QUIT refinement of CATCH,
**		as well as clients like Ren/C++ which need to be told when all
**		errors happen in order to do C++ stack unwinding.  (If automatic
**		re-throwing were used there, Ren/C++ would have no way to
**		insinuate itself as a step in the process when calling Rebol
**		from within a C++ routine invoked by Rebol.  Ponder that.)
**
***********************************************************************/
{
	// You're only supposed to throw an error.
	assert(IS_ERROR(&state->error));

	// !!! Reset or ENABLE_GC; ?

	// Restore elements from initial state, like stack position and frame
	while (DSF != state->dsf) {
		REBINT dsf = DSF;
		SET_DSF(PRIOR_DSF(DSF));

		// !!! Do stuff needed for each call stack frame getting blown away
		// (in StableStack, we must release the stable frame pointer itself)
		/* something done with dsf... */
	}

	DSP = state->dsp;
	GC_Protect->tail = state->hold_tail;
	GC_Disabled = state->gc_disable;

	// Pop state structure (this alone won't restore DSP/DSF/etc)
	Drop_Catch_Helper(state, TRUE);

	// Halts should only be caught by the *topmost* level of
	// execution (generally the interpreter console loop).  There is
	// likely no good reason for a user-exposed ability to CATCH/HALT

	if ((VAL_ERR_NUM(&state->error) == RE_HALT) && Saved_State)
		Throw(&state->error, NULL);

	return TRUE;
}


/***********************************************************************
**
*/	REBOOL Catch_Error_Helper(REBOL_STATE *state)
/*
**		This is the handler which is used with PUSH_CATCH as opposed
**		to PUSH_CATCH_ANY.  The only difference is that it will
**		automatically re-throw an RE_QUIT error without offering
**		an opportunity to process it.  You can only use this form
**		if there's already a PUSH_CATCH_ANY on the stack which
**		can ultimately handle QUITs and HALTs.
**
***********************************************************************/
{
	// If the state has no parent, then it is the topmost level.  It
	// *must* use Catch_Any_Error and be able to handle anything.

	assert(state->last_state);

	// This is a simple wrapper over Catch_Any_Error, and might even
	// be better done in the macro.

	Catch_Any_Error_Helper(state);

	// If you want to catch a QUIT, you must be a construct that uses
	// Catch_Any_Error.  Using Catch_Error_Helper will just rethrow.

	if (VAL_ERR_NUM(&state->error) == RE_QUIT)
		Throw(&state->error, NULL);

	return TRUE;
}


/***********************************************************************
**
*/	void Add_Thrown_Arg_Debug(REBVAL *err, const REBVAL *arg)
/*
**		Sets a task-local value to be associated with the error.
**		At the moment, there is no information put into the
**		actual error itself to reflect this.
**
***********************************************************************/
{
	assert(IS_ERROR(err) && THROWN(err));

	// This assertion is a nice idea, but practically speaking we don't
	// currently have a moment when an error is caught with PUSH_CATCH
	// to set it to trash...only if it has its value processed as a
	// function return or loop break, etc.  One way of fixing it would
	// be to make PUSH_CATCH take 3 arguments instead of 2, and store
	// the error argument in the Rebol_State if it gets thrown...but
	// that looks a bit ugly.  Think more on this.

	/* assert(IS_TRASH(TASK_THROWN_ARG)); */

	*TASK_THROWN_ARG = *arg;
}


/***********************************************************************
**
*/	void Take_Thrown_Arg_Debug(REBVAL *out, const REBVAL *err)
/*
**		Gets the task-local value associated with the error, and
**		sets the task's value to be Trash.  At the moment, there is
**		no linkage between the error and the value.
**
**		WARNING: 'out' can be the same pointer as 'err'
**
***********************************************************************/
{
	assert(IS_ERROR(err) && THROWN(err));

	// See notes about assertion in Add_Thrown_Arg_Debug.  TBD.

	/* assert(!IS_TRASH(TASK_THROWN_ARG)); */

	*out = *TASK_THROWN_ARG;

	// The THROWN_ARG lives under the root set, and must be a value
	// that won't trip up the GC.
	SET_TRASH_SAFE(TASK_THROWN_ARG);
}


/***********************************************************************
**
*/	void Throw(const REBVAL *err, const REBVAL *arg_add)
/*
**		Throw to the enclosing PUSH_CATCH or PUSH_CATCH_ANY.  Although
**		the item type being thrown is a value of type "ERROR!", not
**		all such values represent failures.  The same mechanism is
**		used to do non-local jumps, so the THROWN() categories of
**		error can represent e.g. RETURN or BREAK or CONTINUE.
**
***********************************************************************/
{
	ASSERT_ERROR(err);

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

	// Error may live in a local variable whose stack is going away,
	// on the stack, or other unstable location.  Copy before the jump.

	Saved_State->error = *err;

	// Passing an argument will "add" it to the error, if it is a
	// THROWN-class error like a RETURN that has an associated value.
	// This will assert if you try to add an argument to an error
	// that you are passing on which already had an argument added.

	if (arg_add) {
		assert(THROWN(err));
		ADD_THROWN_ARG(&Saved_State->error, arg_add);
	}

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
*/	void Quit(int status)
/*
**		A QUIT is thrown and can't be caught by a normal CATCH, you
**		have to use a PUSH_CATCH_ANY
**
***********************************************************************/
{
	REBVAL err;
	VAL_SET(&err, REB_ERROR);
	VAL_ERR_NUM(&err) = RE_QUIT;
	VAL_ERR_STATUS(&err) = status;
	Throw(&err, NULL);
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
	REBVAL err;
	VAL_SET(&err, REB_ERROR);
	VAL_ERR_NUM(&err) = RE_HALT;
	Throw(&err, NULL);
}


/***********************************************************************
**
*/	REBCNT Stack_Depth(void)
/*
***********************************************************************/
{
	REBINT dsf = DSF;
	REBCNT count = 0;

	for (dsf = DSF; dsf != DSF_NONE; dsf = PRIOR_DSF(dsf)) {
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
	REBSER *blk = Make_Block(depth-start);
	REBINT dsf;
	REBVAL *val;

	for (dsf = DSF; dsf != DSF_NONE; dsf = PRIOR_DSF(dsf)) {
		if (start-- <= 0) {
			val = Alloc_Tail_Blk(blk);
			Init_Word_Unbound(val, REB_WORD, VAL_WORD_SYM(DSF_LABEL(dsf)));
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
		(cat = VAL_SERIES(BLK_SKIP(cats, n)))
	) {
		Init_Word(&error->type, REB_WORD, FRM_WORD_SYM(cats, n), cats, n);

		// Find word related to the error itself:

		n = code % 100 + 3;
		if (n < SERIES_TAIL(cat))
			Init_Word(&error->id, REB_WORD, FRM_WORD_SYM(cat, n), cat, n);
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
*/	void Make_Error_Object(REBVAL *arg, REBVAL *value)
/*
**		Creates an error object from arg and puts it in value.
**		The arg can be a string or an object body block.
**		This function is called by MAKE ERROR!.
**
***********************************************************************/
{
	REBSER *err;		// Error object
	ERROR_OBJ *error;	// Error object values
	REBINT code = 0;

	VAL_SET(value, REB_ERROR);

	// Create a new error object from another object, including any non-standard fields:
	if (IS_ERROR(arg) || IS_OBJECT(arg)) {
		err = Merge_Frames(VAL_OBJ_FRAME(ROOT_ERROBJ),
			IS_ERROR(arg) ? VAL_OBJ_FRAME(arg) : VAL_ERR_OBJECT(arg));
		error = ERR_VALUES(err);
//		if (!IS_INTEGER(&error->code)) {
			if (!Find_Error_Info(error, &code)) code = RE_INVALID_ERROR;
			SET_INTEGER(&error->code, code);
//		}
		VAL_ERR_NUM(value) = VAL_INT32(&error->code);
		VAL_ERR_OBJECT(value) = err;
		return;
	}

	// Make a copy of the error object template:
	err = CLONE_OBJECT(VAL_OBJ_FRAME(ROOT_ERROBJ));
	error = ERR_VALUES(err);
	SET_NONE(&error->id);
	VAL_SET(value, REB_ERROR);
	VAL_ERR_OBJECT(value) = err;

	// If block arg, evaluate object values (checking done later):
	// If user set error code, use it to setup type and id fields.
	if (IS_BLOCK(arg)) {
		REBVAL ignored; // !!! Is the DO_BLK result meaningful?
		DISABLE_GC;
		Bind_Block(err, VAL_BLK_DATA(arg), BIND_DEEP);
		DO_BLK(&ignored, arg); // GC-OK (disabled)
		ENABLE_GC;
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
		Set_String(&error->arg1, Copy_Series_Value(arg));
		Set_Error_Type(error);
	}

// No longer allowed:
//	else if (IS_INTEGER(arg)) {
//		error->code = *arg;
//		Set_Error_Type(error);
//	}
	else
		Trap_Arg(arg);

	if (!(VAL_ERR_NUM(value) = VAL_INT32(&error->code))) {
		Trap_Arg(arg);
	}
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

	if (PG_Boot_Phase < BOOT_ERRORS) {
		assert(FALSE);
		Panic_Core(RP_EARLY_ERROR, code); // Not far enough!
		DEAD_END;
	}

	// Make a copy of the error object template:
	err = CLONE_OBJECT(VAL_OBJ_FRAME(ROOT_ERROBJ));
	error = ERR_VALUES(err);

	// Set error number:
	SET_INTEGER(&error->code, (REBINT)code);
	Set_Error_Type(error);

	// Set error argument values:
	if (arg1) error->arg1 = *arg1;
	if (arg2) error->arg2 = *arg2;
	if (arg3) error->arg3 = *arg3;

	// Set backtrace and location information:
	if (DSF != DSF_NONE) {
		// Where (what function) is the error:
		Set_Block(&error->where, Make_Backtrace(0));
		// Nearby location of the error (in block being evaluated):
		error->nearest = *DSF_POSITION(DSF);
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

	assert(num >= RE_THROW_MAX);

	VAL_SET(&error, REB_ERROR);
	VAL_ERR_NUM(&error) = num;
	VAL_ERR_OBJECT(&error) = Make_Error(num, 0, 0, 0);
	Throw(&error, NULL);
}


/***********************************************************************
**
*/	void Trap1(REBCNT num, const REBVAL *arg1)
/*
***********************************************************************/
{
	REBVAL error;

	assert(num >= RE_THROW_MAX);

	VAL_SET(&error, REB_ERROR);
	VAL_ERR_NUM(&error) = num;
	VAL_ERR_OBJECT(&error) = Make_Error(num, arg1, 0, 0);
	Throw(&error, NULL);
}


/***********************************************************************
**
*/	void Trap2(REBCNT num, const REBVAL *arg1, const REBVAL *arg2)
/*
***********************************************************************/
{
	REBVAL error;

	assert(num >= RE_THROW_MAX);

	VAL_SET(&error, REB_ERROR);
	VAL_ERR_NUM(&error) = num;
	VAL_ERR_OBJECT(&error) = Make_Error(num, arg1, arg2, 0);
	Throw(&error, NULL);
}


/***********************************************************************
**
*/	void Trap3(REBCNT num, const REBVAL *arg1, const REBVAL *arg2, const REBVAL *arg3)
/*
***********************************************************************/
{
	REBVAL error;

	assert(num >= RE_THROW_MAX);

	VAL_SET(&error, REB_ERROR);
	VAL_ERR_NUM(&error) = num;
	VAL_ERR_OBJECT(&error) = Make_Error(num, arg1, arg2, arg3);
	Throw(&error, NULL);
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
	Init_Word_Unbound(DS_TOP, REB_WORD, sym);
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
*/	REBINT Check_Error(REBVAL *val)
/*
**		Process a loop exceptions. Pass in the TOS value, returns:
**
**			 1 - break (or break/return, which changes val)
**			-1 - if continue, change val to unset
**			 0 - if not break or continue
**			else: error if not an ERROR value
**
***********************************************************************/
{
	// It's UNSET, not an error:
	if (!IS_ERROR(val))
		Trap_DEAD_END(RE_NO_RETURN); //!!! change to special msg

	// If it's a BREAK, check for /return value:
	if (VAL_ERR_NUM(val) == RE_BREAK) {
		TAKE_THROWN_ARG(val, val);
		return 1;
	}

	if (VAL_ERR_NUM(val) == RE_CONTINUE) {
		SET_UNSET(val);
		return -1;
	}

	return 0;
	// Else: Let all other errors return as values.
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
	errs = Construct_Object(0, VAL_BLK(errors), 0);
	Set_Object(Get_System(SYS_CATALOG, CAT_ERRORS), errs);

	// Create objects for all error types:
	for (val = BLK_SKIP(errs, 1); NOT_END(val); val++) {
		errs = Construct_Object(0, VAL_BLK(val), 0);
		SET_OBJECT(val, errs);
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
	for (policy = VAL_BLK(policy); NOT_END(policy); policy += 2) {

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
			Init_Word_Unbound(DS_TOP, REB_WORD, sym);
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
			Init_Word_Unbound(DS_TOP, REB_WORD, sym);
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
	assert(VAL_ERR_NUM(err) != 0);
	if (!THROWN(err)) ASSERT_FRAME(VAL_ERR_OBJECT(err));
}

#endif
