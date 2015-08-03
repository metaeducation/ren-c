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
**  Module:  c-do.c
**  Summary: the core interpreter - the heart of REBOL
**  Section: core
**  Author:  Carl Sassenrath
**  Notes:
**    WARNING WARNING WARNING
**    This is highly tuned code that should only be modified by experts
**    who fully understand its design. It is very easy to create odd
**    side effects so please be careful and extensively test all changes!
**
***********************************************************************/

#include "sys-core.h"
#include <stdio.h>

enum Eval_Types {
	ET_INVALID,		// not valid to evaluate
	ET_WORD,
	ET_SELF,		// returns itself
	ET_FUNCTION,
	ET_OPERATOR,
	ET_PAREN,
	ET_SET_WORD,
	ET_LIT_WORD,
	ET_GET_WORD,
	ET_PATH,
	ET_LIT_PATH,
	ET_END			// end of block
};

/*
void T_Error(REBCNT n) {;}

// Deferred:
void T_Series(REBCNT n) {;}		// image
void T_List(REBCNT n) {;}		// list
*/

void Do_Rebcode(const REBVAL *v) {;}

#include "tmp-evaltypes.h"

#define EVAL_TYPE(val) (Eval_Type_Map[VAL_TYPE(val)])

#define PUSH_ERROR(v, a)
#define PUSH_FUNC(v, w, s)
#define PUSH_BLOCK(b)


/***********************************************************************
**
*/	void Do_Op(const REBVAL *func)
/*
**		A trampoline.
**
***********************************************************************/
{
	Func_Dispatch[VAL_GET_EXT(func) - REB_NATIVE](func);
}


/***********************************************************************
**
*/  REBINT Eval_Depth(void)
/*
***********************************************************************/
{
	REBINT depth = 0;
	REBINT dsf;

	for (dsf = DSF; dsf > 0; dsf = PRIOR_DSF(dsf), depth++);
	return depth;
}


/***********************************************************************
**
*/	REBVAL *Stack_Frame(REBCNT n)
/*
***********************************************************************/
{
	REBINT dsf = DSF;

	for (dsf = DSF; dsf != DSF_NONE; dsf = PRIOR_DSF(dsf)) {
		if (n-- <= 0) return DS_AT(dsf);
	}

	return NULL;
}


/***********************************************************************
**
*/  REBNATIVE(trace)
/*
***********************************************************************/
{
	REBVAL *arg = D_ARG(1);

	Check_Security(SYM_DEBUG, POL_READ, 0);

	// The /back option: ON and OFF, or INTEGER! for # of lines:
	if (D_REF(2)) { // /back
		if (IS_LOGIC(arg)) {
			Enable_Backtrace(VAL_LOGIC(arg));
		}
		else if (IS_INTEGER(arg)) {
			Trace_Flags = 0;
			Display_Backtrace(Int32(arg));
			return R_UNSET;
		}
	}
	else Enable_Backtrace(FALSE);

	// Set the trace level:
	if (IS_LOGIC(arg)) {
		Trace_Level = VAL_LOGIC(arg) ? 100000 : 0;
	}
	else Trace_Level = Int32(arg);

	if (Trace_Level) {
		Trace_Flags = 1;
		if (D_REF(3)) SET_FLAG(Trace_Flags, 1); // function
		Trace_Depth = Eval_Depth() - 1; // subtract current TRACE frame
	}
	else Trace_Flags = 0;

	return R_UNSET;
}

static REBINT Init_Depth(void)
{
	// Check the trace depth is ok:
	int depth = Eval_Depth() - Trace_Depth;
	if (depth < 0 || depth >= Trace_Level) return -1;
	if (depth > 10) depth = 10;
	Debug_Space(4 * depth);
	return depth;
}

#define CHECK_DEPTH(d) if ((d = Init_Depth()) < 0) return;\

void Trace_Line(REBSER *block, REBINT index, const REBVAL *value)
{
	int depth;

	if (GET_FLAG(Trace_Flags, 1)) return; // function
	if (ANY_FUNC(value)) return;

	CHECK_DEPTH(depth);

	Debug_Fmt_(cs_cast(BOOT_STR(RS_TRACE,1)), index+1, value);
	if (IS_WORD(value) || IS_GET_WORD(value)) {
		value = GET_VAR(value);
		if (VAL_TYPE(value) < REB_NATIVE)
			Debug_Fmt_(cs_cast(BOOT_STR(RS_TRACE,2)), value);
		else if (VAL_TYPE(value) >= REB_NATIVE && VAL_TYPE(value) <= REB_FUNCTION)
			Debug_Fmt_(cs_cast(BOOT_STR(RS_TRACE,3)), Get_Type_Name(value), List_Func_Words(value));
		else
			Debug_Fmt_(cs_cast(BOOT_STR(RS_TRACE,4)), Get_Type_Name(value));
	}
	/*if (ANY_WORD(value)) {
		word = value;
		if (IS_WORD(value)) value = GET_VAR(word);
		Debug_Fmt_(cs_cast(BOOT_STR(RS_TRACE,2)), VAL_WORD_FRAME(word), VAL_WORD_INDEX(word), Get_Type_Name(value));
	}
	if (Trace_Stack) Debug_Fmt(cs_cast(BOOT_STR(RS_TRACE,3)), DSP, DSF);
	else
	*/
	Debug_Line();
}

void Trace_Func(const REBVAL *word, const REBVAL *value)
{
	int depth;
	CHECK_DEPTH(depth);
	Debug_Fmt_(cs_cast(BOOT_STR(RS_TRACE,5)), Get_Word_Name(word), Get_Type_Name(value));
	if (GET_FLAG(Trace_Flags, 1)) Debug_Values(DS_AT(DS_ARG_BASE+1), DS_ARGC, 20);
	else Debug_Line();
}

void Trace_Return(const REBVAL *word, const REBVAL *value)
{
	int depth;
	CHECK_DEPTH(depth);
	Debug_Fmt_(cs_cast(BOOT_STR(RS_TRACE,6)), Get_Word_Name(word));
	Debug_Values(value, 1, 50);
}

void Trace_Arg(REBINT num, const REBVAL *arg, const REBVAL *path)
{
	int depth;
	if (IS_REFINEMENT(arg) && (!path || IS_END(path))) return;
	CHECK_DEPTH(depth);
	Debug_Fmt(cs_cast(BOOT_STR(RS_TRACE,6)), num+1, arg);
}


/***********************************************************************
**
*/	void Trace_Value(REBINT n, const REBVAL *value)
/*
***********************************************************************/
{
	int depth;
	CHECK_DEPTH(depth);
	Debug_Fmt(cs_cast(BOOT_STR(RS_TRACE,n)), value);
}

/***********************************************************************
**
*/	void Trace_String(REBINT n, const REBYTE *str, REBINT limit)
/*
***********************************************************************/
{
	static char tracebuf[64];
	int depth;
	int len = MIN(60, limit);
	CHECK_DEPTH(depth);
	memcpy(tracebuf, str, len);
	tracebuf[len] = '\0';
	Debug_Fmt(cs_cast(BOOT_STR(RS_TRACE,n)), tracebuf);
}


/***********************************************************************
**
*/	void Trace_Error(const REBVAL *value)
/*
***********************************************************************/
{
	int depth;
	CHECK_DEPTH(depth);
	Debug_Fmt(cs_cast(BOOT_STR(RS_TRACE, 10)), &VAL_ERR_VALUES(value)->type, &VAL_ERR_VALUES(value)->id);
}


/***********************************************************************
**
*/	REBINT Push_Func(REBVAL *out, REBSER *block, REBCNT index, const REBVAL *label, const REBVAL *func)
/*
**		Push on stack a function call frame as defined in stack.h.
**		Assumes that stack slot for return value has already been pushed.
**		Block value must not be NULL (otherwise will cause GC fault).
**
***********************************************************************/
{
#if !defined(NDEBUG)
	REBINT dsf = DSP;
#endif

	// Temporary solution while still using the data stack for call frames:
	// do an indirection so the 'out' pointer is held in a handle value.
	// This way the REBVAL target can live somewhere other than the data
	// stack.  This needs special GC treatment--see Mark_Call_Frames_Deep()

	DS_PUSH_TRASH;
	VAL_SET(DS_TOP, REB_HANDLE);
	VAL_HANDLE_DATA(DS_TOP) = out;

	// Save prior DSF;
	DS_PUSH_INTEGER(DSF);
	assert(DSF == PRIOR_DSF(dsf));

	// Save current evaluation position
	DS_PUSH_TRASH;
	assert(block); // Don't accept NULL series
	VAL_SET(DS_TOP, REB_BLOCK);
	VAL_SERIES(DS_TOP) = block;
	VAL_INDEX(DS_TOP) = index;
	assert(IS_BLOCK(DSF_WHERE(dsf)));

	// Save symbol describing the function (if we called this as the result of
	// a word or path lookup)
	if (!label) {
		// !!! When a function was not invoked through looking up a word to
		// (or a word in a path) to use as a label, there were three different
		// alternate labels used.  One was SYM__APPLY_, another was
		// ROOT_NONAME, and another was to be the type of the function being
		// executed.  None are fantastic, but we do the type for now.
		DS_PUSH(Get_Type_Word(VAL_TYPE(func)));
	}
	else {
		assert(IS_WORD(label));
		DS_PUSH(label);
	}
	// !!! Not sure why this is needed; seems the label word should be unbound
	// if anything...
	VAL_WORD_FRAME(DS_TOP) = VAL_FUNC_WORDS(func);
	assert(IS_WORD(DSF_LABEL(dsf)));

	// Save FUNC value for safety (spec, args, code):
	DS_PUSH(func);
	assert(ANY_FUNC(DSF_FUNC(dsf)));

	assert(dsf == DSP - DSF_SIZE);

	// frame starts at the return value slot the caller pushed (which will
	// become the value on top of stack when the function call is popped)
	return DSP - DSF_SIZE;
}


/***********************************************************************
**
*/	void Next_Path(REBPVS *pvs)
/*
**		Evaluate next part of a path.
**
***********************************************************************/
{
	REBVAL *path;
	REBPEF func;
	REBVAL temp;

	// Path must have dispatcher, else return:
	func = Path_Dispatch[VAL_TYPE(pvs->value)];
	if (!func) return; // unwind, then check for errors

	pvs->path++;

	//Debug_Fmt("Next_Path: %r/%r", pvs->path-1, pvs->path);

	// object/:field case:
	if (IS_GET_WORD(path = pvs->path)) {
		pvs->select = GET_MUTABLE_VAR(path);
		if (IS_UNSET(pvs->select)) Trap1(RE_NO_VALUE, path);
	}
	// object/(expr) case:
	else if (IS_PAREN(path)) {
		// ?? GC protect stuff !!!!!! stack could expand!
		DO_BLOCK(&temp, VAL_SERIES(path), 0);
		pvs->select = &temp;
	}
	else // object/word and object/value case:
		pvs->select = path;

	// Uses selector on the value.
	// .path - must be advanced as path is used (modified by func)
	// .value - holds currently evaluated path value (modified by func)
	// .select - selector on value
    // .store - storage (usually TOS) for constructed values
	// .setval - non-zero for SET-PATH (set to zero after SET is done)
	// .orig - original path for error messages
	switch (func(pvs)) {
	case PE_OK:
		break;
	case PE_SET: // only sets if end of path
		if (pvs->setval && IS_END(pvs->path+1)) {
			*pvs->value = *pvs->setval;
			pvs->setval = 0;
		}
		break;
	case PE_NONE:
		SET_NONE(pvs->store);
	case PE_USE:
		pvs->value = pvs->store;
		break;
	case PE_BAD_SELECT:
		Trap2(RE_INVALID_PATH, pvs->orig, pvs->path);
	case PE_BAD_SET:
		Trap2(RE_BAD_PATH_SET, pvs->orig, pvs->path);
	case PE_BAD_RANGE:
		Trap_Range(pvs->path);
	case PE_BAD_SET_TYPE:
		Trap2(RE_BAD_FIELD_SET, pvs->path, Of_Type(pvs->setval));
	}

	if (NOT_END(pvs->path+1)) Next_Path(pvs);
}


/***********************************************************************
**
*/	REBVAL *Do_Path(const REBVAL **path_val, REBVAL *val)
/*
**		Evaluate a path value. Path_val is updated so
**		result can be used for function refinements.
**		If val is not zero, then this is a SET-PATH.
**		Returns value only if result is a function,
**		otherwise the result is on TOS.
**
***********************************************************************/
{
	REBPVS pvs;

	if (val && THROWN(val)) {
		// If unwind/throw value is not coming from TOS, push it.
		if (val != DS_TOP) DS_PUSH(val);
		return 0;
	}

	pvs.setval = val;		// Set to this new value
	DS_PUSH_NONE;
	pvs.store = DS_TOP;		// Temp space for constructed results

	// Get first block value:
	pvs.orig = *path_val;
	pvs.path = VAL_BLK_DATA(pvs.orig);

	// Lookup the value of the variable:
	if (IS_WORD(pvs.path)) {
		pvs.value = GET_MUTABLE_VAR(pvs.path);
		if (IS_UNSET(pvs.value)) Trap1_DEAD_END(RE_NO_VALUE, pvs.path);
	} else pvs.value = pvs.path; //Trap2_DEAD_END(RE_INVALID_PATH, pvs.orig, pvs.path);

	// Start evaluation of path:
	if (Path_Dispatch[VAL_TYPE(pvs.value)]) {
		Next_Path(&pvs);
		// Check for errors:
		if (NOT_END(pvs.path+1) && !ANY_FUNC(pvs.value)) {
			// Only function refinements should get by this line:
			Trap2_DEAD_END(RE_INVALID_PATH, pvs.orig, pvs.path);
		}
	}
	else if (NOT_END(pvs.path+1) && !ANY_FUNC(pvs.value))
		Trap2_DEAD_END(RE_BAD_PATH_TYPE, pvs.orig, Of_Type(pvs.value));

	// If SET then we can drop result storage created above.
	if (val) {
		DS_DROP; // on SET, we do not care about returned value
		return 0;
	} else {
		//if (ANY_FUNC(pvs.value) && IS_GET_PATH(pvs.orig)) Debug_Fmt("FUNC %r %r", pvs.orig, pvs.path);
		// If TOS was not used, then copy final value back to it:
		if (pvs.value != pvs.store) *pvs.store = *pvs.value;
		// Return 0 if not function or is :path/word...
		if (!ANY_FUNC(pvs.value) || IS_GET_PATH(pvs.orig)) return 0;
		*path_val = pvs.path; // return new path (for func refinements)
		return pvs.value; // only used for functions
	}
}


/***********************************************************************
**
*/	void Pick_Path(REBVAL *out, REBVAL *value, REBVAL *selector, REBVAL *val)
/*
**		Lightweight version of Do_Path used for A_PICK actions.
**		Result on TOS.
**
***********************************************************************/
{
	REBPVS pvs;
	REBPEF func;

	pvs.value = value;
	pvs.path = 0;
	pvs.select = selector;
	pvs.setval = val;
	pvs.store = out;		// Temp space for constructed results

	// Path must have dispatcher, else return:
	func = Path_Dispatch[VAL_TYPE(value)];
	if (!func) return; // unwind, then check for errors

	switch (func(&pvs)) {
	case PE_OK:
		break;
	case PE_SET: // only sets if end of path
		if (pvs.setval) *pvs.value = *pvs.setval;
		break;
	case PE_NONE:
		SET_NONE(pvs.store);
	case PE_USE:
		pvs.value = pvs.store;
		break;
	case PE_BAD_SELECT:
		Trap2(RE_INVALID_PATH, pvs.value, pvs.select);
	case PE_BAD_SET:
		Trap2(RE_BAD_PATH_SET, pvs.value, pvs.select);
		break;
	}
}


/***********************************************************************
**
*/	static REBINT Do_Args(REBVAL *out, REBINT dsf, const REBVAL *path, REBSER *block, REBCNT index)
/*
**		Evaluate code block according to the function arg spec.
**		Args are pushed onto the data stack in the same order
**		as the function frame.
**
**			dsf: index of function call frame
**			path:  refinements or object/function path
**			block: current evaluation block
**			index: current evaluation index
**
***********************************************************************/
{
#if !defined(NDEBUG)
	REBINT dsp_after_args;
#endif

	REBVAL *value;
	REBVAL *args;
	REBSER *words;
	REBINT ds = 0;			// stack argument position
	REBINT dsp = DSP + 1;	// stack base
	REBVAL *func;

	// We can only assign this *after* the stack expansion (may move it)
	func = DSF_FUNC(dsf);

	// Note we must compensate for first arg already pushed if it is an OP
	assert(dsf == DSP - DSF_SIZE - (IS_OP(func) ? 1 : 0));

	// Get list of words:
	words = VAL_FUNC_WORDS(func);
	args = BLK_SKIP(words, 1);
	ds = SERIES_TAIL(words)-1;	// length of stack fill below
	//Debug_Fmt("Args: %z", VAL_FUNC_WORDS(func));

	// If func is operator, first arg is already on stack:
	if (IS_OP(func)) {
		//if (!TYPE_CHECK(args, VAL_TYPE(DS_AT(DSP))))
		//	Trap3_DEAD_END(RE_EXPECT_ARG, DSF_LABEL(dsf), args, Of_Type(DS_AT(ds)));
		args++;	 	// skip evaluation, but continue with type check
		ds--;		// shorten stack fill below
	}

	// Fill stack variables with default values:
	for (; ds > 0; ds--) DS_PUSH_NONE;

#if !defined(NDEBUG)
	dsp_after_args = DSP;
#endif

	// Go thru the word list args:
	ds = dsp;
	for (; NOT_END(args); args++, ds++) {
		//if (Trace_Flags) Trace_Arg(ds - dsp, args, path);

		// Process each formal argument:
		switch (VAL_TYPE(args)) {

		case REB_WORD:		// WORD - Evaluate next value
			index = Do_Core(out, TRUE, block, index, IS_OP(func));
			if (index == THROWN_FLAG) goto return_index;
			if (index == END_FLAG) Trap2_DEAD_END(RE_NO_ARG, DSF_LABEL(dsf), args);
			*DS_AT(ds) = *out;
			break;

		case REB_LIT_WORD:	// 'WORD - Just get next value
			if (index < BLK_LEN(block)) {
				value = BLK_SKIP(block, index);
				if (IS_PAREN(value) || IS_GET_WORD(value) || IS_GET_PATH(value)) {
					index = Do_Core(out, TRUE, block, index, IS_OP(func));
					if (index == THROWN_FLAG) goto return_index;
					if (index == END_FLAG) {
						// end of block "trick" quotes as an UNSET! (still
						// type checked to see if the parameter accepts it)
						assert(IS_UNSET(out));
					}
					*DS_AT(ds) = *out;
				}
				else {
					index++;
					*DS_AT(ds) = *value;
				}
			} else
				SET_UNSET(DS_AT(ds)); // allowed to be none
			break;

		case REB_GET_WORD:	// :WORD - Get value
			if (index < BLK_LEN(block)) {
				*DS_AT(ds) = *BLK_SKIP(block, index);
				index++;
			} else
				SET_UNSET(DS_AT(ds)); // allowed to be none
			break;

		case REB_REFINEMENT: // /WORD - Function refinement
			if (!path || IS_END(path)) return index;
			if (IS_WORD(path)) {
				// Optimize, if the refinement is the next arg:
				if (SAME_SYM(path, args)) {
					SET_TRUE(DS_AT(ds)); // set refinement stack value true
					path++;				// remove processed refinement
					continue;
				}
				// Refinement out of sequence, resequence arg order:
more_path:
				ds = dsp;
				args = BLK_SKIP(words, 1);
				for (; NOT_END(args); args++, ds++) {
					if (!IS_WORD(path)) {
						Trap1_DEAD_END(RE_BAD_REFINE, path);
					}
					if (IS_REFINEMENT(args) && VAL_WORD_CANON(args) == VAL_WORD_CANON(path)) {
						SET_TRUE(DS_AT(ds)); // set refinement stack value true
						path++;				// remove processed refinement
						break;
					}
				}
				// Was refinement found? If not, error:
				if (IS_END(args)) Trap2_DEAD_END(RE_NO_REFINE, DSF_LABEL(dsf), path);
				continue;
			}
			else Trap1_DEAD_END(RE_BAD_REFINE, path);
			break;

		case REB_SET_WORD:	// WORD: - reserved for special features
		default:
			Trap_Arg_DEAD_END(args);
		}

		// If word is typed, verify correct argument datatype:
		if (!TYPE_CHECK(args, VAL_TYPE(DS_AT(ds))))
			Trap3_DEAD_END(RE_EXPECT_ARG, DSF_LABEL(dsf), args, Of_Type(DS_AT(ds)));
	}

	// Hack to process remaining path:
	if (path && NOT_END(path)) goto more_path;
	//	Trap2_DEAD_END(RE_NO_REFINE, DSF_LABEL(dsf), path);

return_index:
	assert(DSP == dsp_after_args);
	return index;
}


/***********************************************************************
**
*/	void Do_Signals(void)
/*
**		Special events to process during evaluation.
**		Search for SET_SIGNAL to find them.
**
***********************************************************************/
{
	REBCNT sigs;
	REBCNT mask;

	// Accumulate evaluation counter and reset countdown:
	if (Eval_Count <= 0) {
		//Debug_Num("Poll:", (REBINT) Eval_Cycles);
		Eval_Cycles += Eval_Dose - Eval_Count;
		Eval_Count = Eval_Dose;
		if (Eval_Limit != 0 && Eval_Cycles > Eval_Limit)
			Check_Security(SYM_EVAL, POL_EXEC, 0);
	}

	if (!(Eval_Signals & Eval_Sigmask)) return;

	// Be careful of signal loops! EG: do not PRINT from here.
	sigs = Eval_Signals & (mask = Eval_Sigmask);
	Eval_Sigmask = 0;	// avoid infinite loop
	//Debug_Num("Signals:", Eval_Signals);

	// Check for recycle signal:
	if (GET_FLAG(sigs, SIG_RECYCLE)) {
		CLR_SIGNAL(SIG_RECYCLE);
		Recycle();
	}

#ifdef NOT_USED_INVESTIGATE
	if (GET_FLAG(sigs, SIG_EVENT_PORT)) {  // !!! Why not used?
		CLR_SIGNAL(SIG_EVENT_PORT);
		Awake_Event_Port();
	}
#endif

	// Escape only allowed after MEZZ boot (no handlers):
	if (GET_FLAG(sigs, SIG_ESCAPE) && PG_Boot_Phase >= BOOT_MEZZ) {
		CLR_SIGNAL(SIG_ESCAPE);
		Eval_Sigmask = mask;
		Halt();
		DEAD_END_VOID;
	}

	Eval_Sigmask = mask;
}


/***********************************************************************
**
*/	REBCNT Do_Core(REBVAL *out, REBOOL next, REBSER *block, REBCNT index, REBFLG op)
/*
**		Evaluate the code block until we have:
**			1. An irreducible value (return next index)
**			2. Reached the end of the block (return END_FLAG)
**			3. Encountered an error
**
**		Index is a zero-based index into the block.
**		Op indicates infix operator is being evaluated (precedence);
**		The value (or error) is placed on top of the data stack.
**
***********************************************************************/
{
#if !defined(NDEBUG)
	REBINT dsp_orig = DSP;
	REBINT dsp_precall;

	static int count_static = 0;
	int count;
#endif

	REBVAL *value;
	REBINT dsf;

	// Functions don't have "names", though they can be assigned to words.
	// If a function invokes via word lookup (vs. a literal FUNCTION! value),
	// 'label' will be that WORD!, and NULL otherwise.
	const REBVAL *label;

	// Most of what this routine does can be done with value pointers and
	// the data stack.  Some operations need a unit of additional storage.
	// This is a one-REBVAL-sized cell for saving that data.
	REBVAL save;

do_value:
	assert(index != END_FLAG && index != THROWN_FLAG);
	SET_TRASH_SAFE(out);
	label = NULL;

#ifndef NDEBUG
	// This counter is helpful for tracking a specific invocation.
	// If you notice a crash, look on the stack for the topmost call
	// and read the count...then put that here and recompile with
	// a breakpoint set.  (The 'count_static' value is captured into a
	// local 'count' so	you still get the right count after recursion.)
	count = ++count_static;
	if (count ==
		// *** DON'T COMMIT THIS v-- KEEP IT AT ZERO! ***
								  0
		// *** DON'T COMMIT THIS --^ KEEP IT AT ZERO! ***
	) {
		VAL_SET(&save, REB_BLOCK);
		VAL_SERIES(&save) = block;
		VAL_INDEX(&save) = index;
		PROBE_MSG(&save, "Do_Core() count trap");
	}
#endif

	//CHECK_MEMORY(1);
	CHECK_C_STACK_OVERFLOW(&value);

	if (--Eval_Count <= 0 || Eval_Signals) Do_Signals();

	value = BLK_SKIP(block, index);
	//if (Trace_Flags) Trace_Eval(block, index);

	if (Trace_Flags) Trace_Line(block, index, value);

	//getchar();
	switch (EVAL_TYPE(value)) {

	case ET_WORD:
		GET_VAR_INTO(out, value);
		if (IS_UNSET(out)) Trap1_DEAD_END(RE_NO_VALUE, value);
		if (ANY_FUNC(out)) {
			// OP! is only handled by the code at the tail of this routine
			if (IS_OP(out)) Trap_Type_DEAD_END(out);

			// We will reuse the TOS for the OUT of the call frame
			label = value;
			value = out;
			if (Trace_Flags) Trace_Line(block, index, value);
			goto func_needs_push;
		}
		index++;
		break;

	case ET_SELF:
		*out = *value;
		index++;
		break;

	case ET_SET_WORD:
		index = Do_Core(out, TRUE, block, index + 1, FALSE);

		if (index == END_FLAG || VAL_TYPE(out) == REB_UNSET)
			Trap1_DEAD_END(RE_NEED_VALUE, value);

		if (index == THROWN_FLAG) goto return_index;

		Set_Var(value, out);
		break;

	case ET_FUNCTION:

	// Value must be the function, and space for the return slot (DSF_OUT)
	// needs to already be accounted for
	func_needs_push:
		assert(ANY_FUNC(value));
		assert(DSP == dsp_orig);
		dsf = Push_Func(out, block, index, label, value);
		SET_TRASH_SAFE(out); // catch functions that don't write out

	// 'dsf' holds index of new call frame, not yet set during arg evaluation
	// (because the arguments want to be computed in the caller's environment)
	// value can be invalid at this point, but must be retrievable w/DSF_FUNC
	func_already_pushed:
		assert(IS_TRASH(out));
		assert(DSF == -1 || dsf > DSF);
		index = Do_Args(out, dsf, 0, block, index+1);

	// The function frame is completely filled with arguments and ready
	func_ready_to_call:
		assert(DSF == -1 || dsf > DSF);
		value = DSF_FUNC(dsf);
		assert(ANY_FUNC(value));

		// if THROW, RETURN, BREAK, CONTINUE during Do_Args
		if (index == THROWN_FLAG) {
			// Free the pushed function call frame
			DS_DROP_TO(dsf);
			goto return_index;
		}

		// If the last value Do_Args evaluated wasn't thrown, we don't
		// need to pay attention to it here.

		SET_TRASH_SAFE(out);

	#if !defined(NDEBUG)
		dsp_precall = DSP;
	#endif

		// The arguments were successfully acquired, so we set the
		// the DSF to our constructed 'dsf' during the Push_Func...then
		// call the function...then put the DSF back to the call level
		// of whoever called us.

		SET_DSF(dsf);
		if (Trace_Flags) Trace_Func(label, value);
		Func_Dispatch[VAL_TYPE(value) - REB_NATIVE](value);

	#if !defined(NDEBUG)
		assert(DSP >= dsp_precall);
		if (DSP > dsp_precall) {
			PROBE_MSG(DSF_WHERE(dsf), "UNBALANCED STACK TRAP!!!");
			Panic(RP_MISC);
		}
	#endif

		SET_DSF(PRIOR_DSF(dsf));

		// Drop stack back to where the DSF_OUT(dsf) is now the Top of Stack
		DS_DROP_TO(dsf);

		if (THROWN(out)) {
			index = THROWN_FLAG;
			goto return_index;
		}

		// Function execution should have written *some* actual output value
		// over the trash that we put in the return slot before the call.
		assert(!IS_TRASH(out));

		if (Trace_Flags) Trace_Return(label, out);

		// The return value is a FUNC that needs to be re-evaluated.
		if (VAL_GET_OPT(out, OPTS_REVAL) && ANY_FUNC(out)) {
			value = out;

			if (IS_OP(value)) Trap_Type_DEAD_END(value); // not allowed

			label = NULL;
			index--; // Backup block index to re-evaluate.

			goto func_needs_push;
		}
		break;

	case ET_OPERATOR:
		// Can't actually run an OP! arg unless it's after an evaluation
		Trap1_DEAD_END(RE_NO_OP_ARG, label);

	handle_op:
		assert(index != 0);
		// TOS has first arg, we will re-use that slot for the OUT value
		dsf = Push_Func(out, block, index, label, value);
		DS_PUSH(out); // Copy prior to first argument
		SET_TRASH_SAFE(out); // catch functions that don't write out
		goto func_already_pushed;

	case ET_PATH:  // PATH, SET_PATH
		label = value; // a path
		//index++; // now done below with +1

		if (IS_SET_PATH(value)) {
			index = Do_Core(out, TRUE, block, index + 1, FALSE);
			// THROWN is handled in Do_Path.
			if (index == END_FLAG || VAL_TYPE(out) <= REB_UNSET)
				Trap1_DEAD_END(RE_NEED_VALUE, label);
			Do_Path(&label, out);
		}
		else { // Can be a path or get-path:

			// returns in word the path item, DS_TOP has value
			value = Do_Path(&label, 0);
			DS_POP_INTO(out);

			// Value returned only for functions that need evaluation (but not GET_PATH):
			if (value && ANY_FUNC(value)) {
				// object/func or func/refinements or object/func/refinement:

				if (label && !IS_WORD(label))
					Trap1(RE_BAD_REFINE, label); // CC#2226

				// Cannot handle an OP! because prior value is wiped out above
				// (Theoretically we could save it if we are DO-ing a chain of
				// values, and make it work.  But then, a loop of DO/NEXT
				// may not behave the same as DO-ing the whole block.  Bad.)

				if (IS_OP(value)) Trap_Type_DEAD_END(value);

				dsf = Push_Func(out, block, index, label, value);

				index = Do_Args(out, dsf, label + 1, block, index + 1);

				// We now refresh the function value because Do may have moved
				// the stack.  With the function value saved, we default the
				// function output to UNSET!
				value = DSF_FUNC(dsf);

				goto func_ready_to_call;
			} else
				index++;
		}
		break;

	case ET_PAREN:
		if (!DO_BLOCK(out, VAL_SERIES(value), 0)) {
			index = THROWN_FLAG;
			goto return_index;
		}
		index++;
		break;

	case ET_LIT_WORD:
		*out = *value;
		VAL_SET(out, REB_WORD);
		index++;
		break;

	case ET_GET_WORD:
		GET_VAR_INTO(out, value);
		index++;
		break;

	case ET_LIT_PATH:
		// !!! Aliases a REBSER under two value types, likely bad, see CC#2233
		*out = *value;
		VAL_SET(out, REB_PATH);
		index++;
		break;

	case ET_END:
		SET_UNSET(out);
		return END_FLAG;

	case ET_INVALID:
		Trap1(RE_NO_VALUE, value);
		DEAD_END;

	default:
		//Debug_Fmt("Bad eval: %d %s", VAL_TYPE(value), Get_Type_Name(value));
		assert(FALSE);
		Panic_Core(RP_BAD_EVALTYPE, VAL_TYPE(value));
		DEAD_END;
		//return -index;
	}

	// If normal eval (not higher precedence of infix op), check for op:
	if (!op) {
		value = BLK_SKIP(block, index);

		// Literal function OP! values may occur.
		if (IS_OP(value)) {
			label = NULL;
			if (Trace_Flags) Trace_Line(block, index, value);
			goto handle_op;
		}

		// WORD! values may look up to an OP!
		if (IS_WORD(value) && VAL_WORD_FRAME(value)) {
			label = value;
			GET_VAR_INTO(&save, value);
			if (IS_OP(&save)) {
				value = &save;
				if (Trace_Flags) Trace_Line(block, index, value);
				goto handle_op;
			}
		}
	}

	// Should not have accumulated any net data stack during the evaluation
	assert(DSP == dsp_orig);

	// Should not have a THROWN value if we got here
	assert(index != THROWN_FLAG && !THROWN(out));

	// Continue evaluating rest of block if not just a DO/NEXT
	if (index < BLK_LEN(block) && !next) goto do_value;

return_index:
	assert(DSP == dsp_orig);
	assert(!IS_TRASH(out));
	assert((index == THROWN_FLAG) == THROWN(out));
	assert(index != END_FLAG || index >= BLK_LEN(block));
	return index;
}


/***********************************************************************
**
*/	void Reduce_Block(REBVAL *out, REBSER *block, REBCNT index, REBOOL into)
/*
**		Reduce block from the index position specified in the value.
**		Collect all values from stack and make them a block.
**
***********************************************************************/
{
	REBINT dsp_orig = DSP;

	while (index < BLK_LEN(block)) {
		REBVAL reduced;
		index = DO_NEXT(&reduced, block, index);
		if (index == THROWN_FLAG) {
			*out = reduced;
			DS_DROP_TO(dsp_orig);
			goto finished;
		}
		DS_PUSH(&reduced);
	}

	Pop_Stack_Values(out, dsp_orig, into);

finished:
	assert(DSP == dsp_orig);
}


/***********************************************************************
**
*/	void Reduce_Only(REBVAL *out, REBSER *block, REBCNT index, REBVAL *words, REBOOL into)
/*
**		Reduce only words and paths not found in word list.
**
***********************************************************************/
{
	REBINT dsp_orig = DSP;
	REBVAL *val;
	const REBVAL *v;
	REBSER *ser = 0;
	REBCNT idx = 0;

	if (IS_BLOCK(words)) {
		ser = VAL_SERIES(words);
		idx = VAL_INDEX(words);
	}

	for (val = BLK_SKIP(block, index); NOT_END(val); val++) {
		if (IS_WORD(val)) {
			// Check for keyword:
			if (ser && NOT_FOUND != Find_Word(ser, idx, VAL_WORD_CANON(val))) {
				DS_PUSH(val);
				continue;
			}
			v = GET_VAR(val);
			DS_PUSH(v);
		}
		else if (IS_PATH(val)) {
			const REBVAL *v;

			if (ser) {
				// Check for keyword/path:
				v = VAL_BLK_DATA(val);
				if (IS_WORD(v)) {
					if (NOT_FOUND != Find_Word(ser, idx, VAL_WORD_CANON(v))) {
						DS_PUSH(val);
						continue;
					}
				}
			}

			v = val;

			// pushes val on stack
			Do_Path(&v, NULL);
		}
		else DS_PUSH(val);
		// No need to check for unwinds (THROWN) here, because unwinds should
		// never be accessible via words or paths.
	}

	Pop_Stack_Values(out, dsp_orig, into);

	assert(DSP == dsp_orig);
}


/***********************************************************************
**
*/	void Reduce_Block_No_Set(REBVAL *out, REBSER *block, REBCNT index, REBOOL into)
/*
***********************************************************************/
{
	REBINT dsp_orig = DSP;

	while (index < BLK_LEN(block)) {
		REBVAL *value = BLK_SKIP(block, index);
		if (IS_SET_WORD(value)) {
			DS_PUSH(value);
			index++;
		}
		else {
			REBVAL reduced;
			index = DO_NEXT(&reduced, block, index);
			if (index == THROWN_FLAG) {
				*out = reduced;
				DS_DROP_TO(dsp_orig);
				goto finished;
			}
			DS_PUSH(&reduced);
		}
	}

	Pop_Stack_Values(out, dsp_orig, into);

finished:
	assert(DSP == dsp_orig);
}


/***********************************************************************
**
*/	void Reduce_Type_Stack(REBSER *block, REBCNT index, REBCNT type)
/*
**		Reduce a block of words/paths that are of the specified type.
**		Return them on the stack. The change in TOS is the length.
**
***********************************************************************/
{
	//REBINT start = DSP + 1;
	REBVAL *val;

	// Lookup words and paths and push values on stack:
	for (val = BLK_SKIP(block, index); NOT_END(val); val++) {
		if (IS_WORD(val)) {
			const REBVAL *v = GET_VAR(val);
			if (VAL_TYPE(v) == type) DS_PUSH(v);
		}
		else if (IS_PATH(val)) {
			const REBVAL *v = val;
			if (!Do_Path(&v, 0)) { // pushes val on stack
				if (VAL_TYPE(DS_TOP) != type) DS_DROP;
			}
		}
		else if (VAL_TYPE(val) == type) DS_PUSH(val);
		// !!! check stack size
	}

	//block = Copy_Values(DS_Base + start, DSP - start + 1);
	//DSP = start;
	//return block;
}


/***********************************************************************
**
*/	void Reduce_In_Frame(REBSER *frame, REBVAL *values)
/*
**		Reduce a block with simple lookup in the context.
**		Only words in that context are valid (e.g. error object).
**		All values are left on the stack. No copy is made.
**
***********************************************************************/
{
	REBVAL *val;

	for (; NOT_END(values); values++) {
		switch (VAL_TYPE(values)) {
		case REB_WORD:
		case REB_SET_WORD:
		case REB_GET_WORD:
			if ((val = Find_Word_Value(frame, VAL_WORD_SYM(values)))) {
				DS_PUSH(val);
				break;
			} // Unknown in context, fall below, use word as value.
		case REB_LIT_WORD:
			DS_PUSH(values);
			VAL_SET(DS_TOP, REB_WORD);
			break;
		default:
			DS_PUSH(values);
		}
	}
}


/***********************************************************************
**
*/	void Compose_Block(REBVAL *out, REBVAL *block, REBFLG deep, REBFLG only, REBOOL into)
/*
**		Compose a block from a block of un-evaluated values and
**		paren blocks that are evaluated.  Performs evaluations, so
**		if 'into' is provided, then its series must be protected from
**		garbage collection.
**
**			deep - recurse into sub-blocks
**			only - parens that return blocks are kept as blocks
**
**		Writes result value at address pointed to by out.
**
***********************************************************************/
{
	REBVAL *value;
	REBINT dsp_orig = DSP;

	for (value = VAL_BLK_DATA(block); NOT_END(value); value++) {
		if (IS_PAREN(value)) {
			REBVAL evaluated;

			if (!DO_BLOCK(&evaluated, VAL_SERIES(value), 0)) {
				// throw, return, break, continue...
				*out = evaluated;
				DS_DROP_TO(dsp_orig);
				goto finished;
			}

			if (IS_BLOCK(&evaluated) && !only) {
				// compose [blocks ([a b c]) merge] => [blocks a b c merge]
				Push_Stack_Values(
					cast(REBVAL*, VAL_BLK_DATA(&evaluated)),
					VAL_BLK_LEN(&evaluated)
				);
			}
			else if (!IS_UNSET(&evaluated)) {
				// compose [(1 + 2) inserts as-is] => [3 inserts as-is]
				// compose/only [([a b c]) unmerged] => [[a b c] unmerged]
				DS_PUSH(&evaluated);
			}
			else {
				// compose [(print "Unsets *vanish*!")] => []
			}
		}
		else if (deep) {
			if (IS_BLOCK(value)) {
				// compose/deep [does [(1 + 2)] nested] => [does [3] nested]
				REBVAL composed;
				Compose_Block(&composed, value, TRUE, only, into);
				DS_PUSH(&composed);
			}
			else {
				DS_PUSH(value);
				if (ANY_BLOCK(value)) {
					// compose [copy/(orig) (copy)] => [copy/(orig) (copy)]
					// !!! path and second paren are copies, first paren isn't
					VAL_SERIES(DS_TOP) = Copy_Block(VAL_SERIES(value), 0);
				}
			}
		}
		else {
			// compose [[(1 + 2)] (reverse "wollahs")] => [[(1 + 2)] "shallow"]
			DS_PUSH(value);
		}
	}

	Pop_Stack_Values(out, dsp_orig, into);

finished:
	assert(DSP == dsp_orig);
}


/***********************************************************************
**
*/	void Apply_Block(REBVAL *out, const REBVAL *func, REBVAL *args, REBFLG reduce)
/*
**		Result is on top of stack.
**
***********************************************************************/
{
	REBINT ftype = VAL_TYPE(func) - REB_NATIVE; // function type
	REBSER *block = VAL_SERIES(args);
	REBCNT index = VAL_INDEX(args);
	REBINT dsf;

	REBSER *words;
	REBINT len;
	REBINT n;
	REBVAL *val;

	if (index > SERIES_TAIL(block)) index = SERIES_TAIL(block);

	// Push function frame:
	SET_TRASH_SAFE(out);
	dsf = Push_Func(out, block, index, NULL, func);
	func = DSF_FUNC(dsf); // for safety

	// Determine total number of args:
	words = VAL_FUNC_WORDS(func);
	len = words ? SERIES_TAIL(words)-1 : 0;

	// Gather arguments:
	if (reduce) {
		// Reduce block contents to stack:
		n = 0;
		while (index < BLK_LEN(block)) {
			DS_PUSH_TRASH;
			index = DO_NEXT(DS_TOP, block, index);
			if (index == THROWN_FLAG) {
				*out = *DS_TOP;
				goto return_balanced;
			}
			n++;
		}
	}
	else {
		// Copy block contents to stack:
		n = VAL_BLK_LEN(args);
		if (len < n) n = len;
		Push_Stack_Values(BLK_SKIP(block, index), n);
	}

	// Pad out missing args:
	for (; n < len; n++) DS_PUSH_NONE;

	// Validate arguments:
	if (words) {
		val = DSF_ARG(dsf, 1);
		for (args = BLK_SKIP(words, FIRST_PARAM_INDEX); NOT_END(args);) {
			// If arg is refinement, determine its state:
			if (IS_REFINEMENT(args)) {
				if (IS_CONDITIONAL_FALSE(val)) {
					SET_NONE(val);  // ++ ok for none
					while (TRUE) {
						val++;
						args++;
						if (IS_END(args) || IS_REFINEMENT(args)) break;
						SET_NONE(val);
					}
					continue;
				}
				SET_TRUE(val);
			}
			// If arg is typed, verify correct argument datatype:
			if (!TYPE_CHECK(args, VAL_TYPE(val)))
				Trap3(RE_EXPECT_ARG, DSF_LABEL(dsf), args, Of_Type(val));
			args++;
			val++;
		}
	}

	// Evaluate the function:
	SET_TRASH_SAFE(out);
	SET_DSF(dsf);
	Func_Dispatch[ftype](func);
	SET_DSF(PRIOR_DSF(dsf));

return_balanced:
	DS_DROP_TO(dsf); // put data stack back where it was when we were called
}


/***********************************************************************
**
*/	void Apply_Function(REBVAL *out, const REBVAL *func, va_list *args)
/*
**		(va_list by pointer: http://stackoverflow.com/a/3369762/211160)
**
**		Applies function from args provided by C call. Zero terminated.
**		Result returned on TOS
**
**		func - function to call
**		args - list of function args (null terminated)
**
***********************************************************************/
{
	REBINT dsf;
	REBSER *words;
	REBCNT ds;
	REBVAL *arg;

	REBSER *wblk; // where block (where we were called)
	REBCNT widx; // where index (position in above block)

	// For debugging purposes, DO wants to know what our execution
	// block and position are.  We have to make something up, because
	// this call is originating from C code (not Rebol code).
	if (DSF != DSF_NONE) {
		// Some function is on the stack, so fabricate our execution
		// position by copying the block and position it was at.

		wblk = VAL_SERIES(DSF_WHERE(DSF));
		widx = VAL_INDEX(DSF_WHERE(DSF));
	}
	else if (IS_FUNCTION(func) || IS_CLOSURE(func)) {
		// Stack is empty, so offer up the body of the function itself
		// (if it has a body!)
		wblk = VAL_FUNC_BODY(func);
		widx = 0;
	}
	else {
		// We got nothin'.  Give back the specially marked "top level"
		// empty block just to provide something in the slot
		// !!! Could use more sophisticated backtracing here, and in general
		wblk = EMPTY_SERIES;
		widx = 0;
	}

	SET_TRASH_SAFE(out);
	dsf = Push_Func(out, wblk, widx, NULL, func);
	func = DSF_FUNC(dsf); // for safety
	words = VAL_FUNC_WORDS(func);
	ds = SERIES_TAIL(words)-1;	// length of stack fill below

	// Gather arguments from C stack:
	for (; ds > 0; ds--) {
		arg = va_arg(*args, REBVAL*); // get value
		if (arg) DS_PUSH(arg);  // push it; no type check
		else break;
	}
	for (; ds > 0; ds--) DS_PUSH_NONE; // unused slots

	// Evaluate the function:
	SET_DSF(dsf);
	Func_Dispatch[VAL_TYPE(func) - REB_NATIVE](func);
	SET_DSF(PRIOR_DSF(dsf));
	DS_DROP_TO(dsf);
}


/***********************************************************************
**
*/	void Apply_Func(REBVAL *out, REBVAL *func, ...)
/*
**		Applies function from args provided by C call. Zero terminated.
**		Return value is on TOS
**
***********************************************************************/
{
	va_list args;

	if (!ANY_FUNC(func)) Trap_Arg(func);

	va_start(args, func);
	Apply_Function(out, func, &args);
	va_end(args);
}


/***********************************************************************
**
*/	void Do_Sys_Func(REBVAL *out, REBCNT inum, ...)
/*
**		Evaluates a SYS function and TOS contains the result.
**
***********************************************************************/
{
	va_list args;
	REBVAL *value = FRM_VALUE(Sys_Context, inum);

	if (!ANY_FUNC(value)) Trap1(RE_BAD_SYS_FUNC, value);

	va_start(args, inum);
	Apply_Function(out, value, &args);
	va_end(args);
}


/***********************************************************************
**
*/	void Do_Construct(REBVAL *value)
/*
**		Do a block with minimal evaluation and no evaluation of
**		functions. Used for things like script headers where security
**		is important.
**
**		Handles cascading set words:  word1: word2: value
**
***********************************************************************/
{
	REBVAL *temp;
	REBINT ssp;  // starting stack pointer

	DS_PUSH_NONE;
	temp = DS_TOP;
	ssp = DSP;

	for (; NOT_END(value); value++) {
		if (IS_SET_WORD(value)) {
			// Next line not needed, because SET words are ALWAYS in frame.
			//if (VAL_WORD_INDEX(value) > 0 && VAL_WORD_FRAME(value) == frame)
				DS_PUSH(value);
		} else {
			// Get value:
			if (IS_WORD(value)) {
				switch (VAL_WORD_CANON(value)) {
				case SYM_NONE:
					SET_NONE(temp);
					break;
				case SYM_TRUE:
				case SYM_ON:
				case SYM_YES:
					SET_TRUE(temp);
					break;
				case SYM_FALSE:
				case SYM_OFF:
				case SYM_NO:
					SET_FALSE(temp);
					break;
				default:
					*temp = *value;
					VAL_SET(temp, REB_WORD);
				}
			}
			else if (IS_LIT_WORD(value)) {
				*temp = *value;
				VAL_SET(temp, REB_WORD);
			}
			else if (IS_LIT_PATH(value)) {
				*temp = *value;
				VAL_SET(temp, REB_PATH);
			}
			else if (VAL_TYPE(value) >= REB_NONE) { // all valid values
				*temp = *value;
			}
			else
				SET_NONE(temp);

			// Set prior set-words:
			while (DSP > ssp) {
				Set_Var(DS_TOP, temp);
				DS_DROP;
			}
		}
	}
	DS_DROP; // temp
}


/***********************************************************************
**
*/	void Do_Min_Construct(REBVAL *value)
/*
**		Do no evaluation of the set values.
**
***********************************************************************/
{
	REBVAL *temp;
	REBINT ssp;  // starting stack pointer

	DS_PUSH_NONE;
	temp = DS_TOP;
	ssp = DSP;

	for (; NOT_END(value); value++) {
		if (IS_SET_WORD(value)) {
			// Next line not needed, because SET words are ALWAYS in frame.
			//if (VAL_WORD_INDEX(value) > 0 && VAL_WORD_FRAME(value) == frame)
				DS_PUSH(value);
		} else {
			// Get value:
			*temp = *value;
			// Set prior set-words:
			while (DSP > ssp) {
				Set_Var(DS_TOP, temp);
				DS_DROP;
			}
		}
	}
	DS_DROP; // temp
}


/***********************************************************************
**
*/	void Call_Func(REBVAL *func_val)
/*
**		Calls a REBOL function from C code.
**
**	Setup:
**		Before calling this, the caller must setup the stack and
**		provide the function arguments on the stack. Any missing
**		args will be set to NONE.
**
**	Return:
**		On return, the stack remains as-is. The caller must reset
**		the DSP and DSF values.
**
***********************************************************************/
{
	REBINT n;

	// Caller must: Prep_Func + Args above
	VAL_WORD_FRAME(DSF_LABEL(DSF)) = VAL_FUNC_WORDS(func_val);
	n = DS_ARGC - (SERIES_TAIL(VAL_FUNC_WORDS(func_val)) - 1);
	for (; n > 0; n--) DS_PUSH_NONE;
	Func_Dispatch[VAL_TYPE(func_val)-REB_NATIVE](func_val);
	// Caller must: pop stack back
}


/***********************************************************************
**
*/	void Redo_Func(REBVAL *func_val)
/*
**		Trampoline a function, restacking arguments as needed.
**
**	Setup:
**		The source for arguments is the existing stack frame,
**		or a prior stack frame. (Prep_Func + Args)
**
***********************************************************************/
{
	REBSER *wsrc;		// words of source func
	REBSER *wnew;		// words of target func
	REBCNT isrc;		// index position in source frame
	REBCNT inew;		// index position in target frame
	REBVAL *word;
	REBVAL *word2;
	REBINT dsp_orig = DSP;

	REBINT dsf;

	wsrc = VAL_FUNC_WORDS(DSF_FUNC(DSF));
	wnew = VAL_FUNC_WORDS(func_val);

	// As part of the "Redo" we are not adding a new function location,
	// label, or place to write the output.  We are substituting new code
	// and perhaps adjusting the arguments in our re-doing call.

	dsf = Push_Func(
		DSF_OUT(DSF),
		VAL_SERIES(DSF_WHERE(DSF)),
		VAL_INDEX(DSF_WHERE(DSF)),
		DSF_LABEL(DSF),
		func_val
	);

	// Foreach arg of the target, copy to source until refinement.
	for (isrc = inew = FIRST_PARAM_INDEX; inew < BLK_LEN(wnew); inew++, isrc++) {
		word = BLK_SKIP(wnew, inew);
		if (isrc > BLK_LEN(wsrc)) isrc = BLK_LEN(wsrc);

		switch (VAL_TYPE(word)) {
			case REB_SET_WORD: // !!! for definitional return...
				assert(FALSE); // !!! (but not yet)
			case REB_WORD:
			case REB_LIT_WORD:
			case REB_GET_WORD:
				if (VAL_TYPE(word) == VAL_TYPE(BLK_SKIP(wsrc, isrc))) {
					DS_PUSH(DSF_ARG(DSF, isrc));
					// !!! Should check datatypes for new arg passing!
				}
				else {
					// !!! Why does this allow the bounced-to function to have
					// a different type, push a none, and not 'Trap_Arg(word);'
					DS_PUSH_NONE;
				}
				break;

			// At refinement, search for it in source, then continue with words.
			case REB_REFINEMENT:
				// Are we aligned on the refinement already? (a common case)
				word2 = BLK_SKIP(wsrc, isrc);
				if (
					IS_REFINEMENT(word2)
					&& VAL_WORD_CANON(word2) == VAL_WORD_CANON(word)
				) {
					DS_PUSH(DSF_ARG(DSF, isrc));
				}
				else {
					// No, we need to search for it:
					for (isrc = FIRST_PARAM_INDEX; isrc < BLK_LEN(wsrc); isrc++) {
						word2 = BLK_SKIP(wsrc, isrc);
						if (
							IS_REFINEMENT(word2)
							&& VAL_WORD_CANON(word2) == VAL_WORD_CANON(word)
						) {
							DS_PUSH(DSF_ARG(DSF, isrc));
							break;
						}
					}
					// !!! The function didn't have the refinement so skip
					// it.  But what will happen now with the arguments?
					DS_PUSH_NONE;
					//if (isrc >= BLK_LEN(wsrc)) Trap_Arg(word);
				}
				break;

			default:
				Panic(RP_MISC);
		}
	}

	// !!! Temporary; there's a better factoring where we don't have this
	// dispatch duplicated coming...

	SET_DSF(dsf);

	Func_Dispatch[VAL_TYPE(func_val)-REB_NATIVE](func_val);
	SET_DSF(PRIOR_DSF(dsf));

	DS_DROP_TO(dsp_orig);
}


/***********************************************************************
**
*/	const REBVAL *Get_Simple_Value(const REBVAL *val)
/*
**		Does easy lookup, else just returns the value as is.
**
**      !!! What's with leaving path! values on the stack?!?  :-/
**
***********************************************************************/
{
	if (IS_WORD(val) || IS_GET_WORD(val))
		val = GET_VAR(val);
	else if (IS_PATH(val) || IS_GET_PATH(val)) {
		// !!! Temporary: make a copy to pass mutable value to Do_Path
		REBVAL path = *val;
		const REBVAL *v = &path;
		DS_PUSH_NONE;
		Do_Path(&v, 0);
		val = DS_TOP;
	}

	return val;
}


/***********************************************************************
**
*/	REBSER *Resolve_Path(REBVAL *path, REBCNT *index)
/*
**		Given a path, return a context and index for its terminal.
**
***********************************************************************/
{
	REBVAL *sel; // selector
	const REBVAL *val;
	REBSER *blk;
	REBCNT i;

	if (VAL_TAIL(path) < 2) return 0;
	blk = VAL_SERIES(path);
	sel = BLK_HEAD(blk);
	if (!ANY_WORD(sel)) return 0;
	val = GET_VAR(sel);

	sel = BLK_SKIP(blk, 1);
	while (TRUE) {
		if (!ANY_OBJECT(val) || !IS_WORD(sel)) return 0;
		i = Find_Word_Index(VAL_OBJ_FRAME(val), VAL_WORD_SYM(sel), FALSE);
		sel++;
		if (IS_END(sel)) {
			*index = i;
			return VAL_OBJ_FRAME(val);
		}
	}

	return 0; // never happens
}
