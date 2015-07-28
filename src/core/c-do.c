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

void Do_Rebcode(REBVAL *v) {;}

#include "tmp-evaltypes.h"

#define EVAL_TYPE(val) (Eval_Type_Map[VAL_TYPE(val)])

#define PUSH_ERROR(v, a)
#define PUSH_FUNC(v, w, s)
#define PUSH_BLOCK(b)


/***********************************************************************
**
*/	void Do_Op(REBVAL *func)
/*
**		A trampoline.
**
***********************************************************************/
{
	Func_Dispatch[VAL_GET_EXT(func) - REB_NATIVE](func);
}


/***********************************************************************
**
*/	void Expand_Stack(REBCNT amount)
/*
**		Expand the datastack. Invalidates any references to stack
**		values, so code should generally use stack index integers,
**		not pointers into the stack.
**
***********************************************************************/
{
	// !!! Temporary for StableStack simulation... we never expand
	Panic(RP_MISC);

	if (SERIES_REST(DS_Series) >= STACK_LIMIT) Trap(RE_STACK_OVERFLOW);
	DS_Series->tail = DSP+1;
	Extend_Series(DS_Series, amount);
	DS_Base = BLK_HEAD(DS_Series);
	Debug_Fmt(cs_cast(BOOT_STR(RS_STACK, 0)), DSP, SERIES_REST(DS_Series));
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
	REBCNT dsf = DSF;

	for (dsf = DSF; dsf > 0; dsf = PRIOR_DSF(dsf)) {
		if (n-- <= 0) return DS_VALUE(dsf);
	}

	return 0;
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

void Trace_Func(REBVAL *word, REBVAL *value)
{
	int depth;
	CHECK_DEPTH(depth);
	Debug_Fmt_(cs_cast(BOOT_STR(RS_TRACE,5)), Get_Word_Name(word), Get_Type_Name(value));
	if (GET_FLAG(Trace_Flags, 1)) Debug_Values(DS_GET(DS_ARG_BASE+1), DS_ARGC, 20);
	else Debug_Line();
}

void Trace_Return(REBVAL *word, REBVAL *value)
{
	int depth;
	CHECK_DEPTH(depth);
	Debug_Fmt_(cs_cast(BOOT_STR(RS_TRACE,6)), Get_Word_Name(word));
	Debug_Values(value, 1, 50);
}

void Trace_Arg(REBINT num, REBVAL *arg, REBVAL *path)
{
	int depth;
	if (IS_REFINEMENT(arg) && (!path || IS_END(path))) return;
	CHECK_DEPTH(depth);
	Debug_Fmt(cs_cast(BOOT_STR(RS_TRACE,6)), num+1, arg);
}


/***********************************************************************
**
*/	void Trace_Value(REBINT n, REBVAL *value)
/*
***********************************************************************/
{
	int depth;
	CHECK_DEPTH(depth);
	Debug_Fmt(cs_cast(BOOT_STR(RS_TRACE,n)), value);
}

/***********************************************************************
**
*/	void Trace_String(REBINT n, REBYTE *str, REBINT limit)
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
*/	void Trace_Error(REBVAL *value)
/*
***********************************************************************/
{
	int depth;
	CHECK_DEPTH(depth);
	Debug_Fmt(cs_cast(BOOT_STR(RS_TRACE, 10)), &VAL_ERR_VALUES(value)->type, &VAL_ERR_VALUES(value)->id);
}


/***********************************************************************
**
*/	REBINT Push_Func(REBSER *block, REBCNT index, const REBVAL *label, const REBVAL *func)
/*
**		Push on stack a function call frame as defined in stack.h.
**		Assumes that stack slot for return value has already been pushed.
**		Block value must not be NULL (otherwise will cause GC fault).
**
***********************************************************************/
{
#if !defined(NDEBUG)
	// account for already pushed return value.  e.g. DSP starts out at 0,
	// caller pushes a return value and DSP is 1.  Our dsf value that we
	// return will thus be 1; which is where we want to drop the stack to
	// when the function call is completed, so the return value is TOS
	REBINT dsf = DSP;
#endif

	// Save prior DSF;
	DS_SKIP;
	SET_INTEGER(DS_TOP, DSF);
	assert(DSF == PRIOR_DSF(dsf));

	// Save current evaluation position
	DS_SKIP;
	assert(block); // Don't accept NULL series
	VAL_SET(DS_TOP, REB_BLOCK);
	VAL_SERIES(DS_TOP) = block;
	VAL_INDEX(DS_TOP) = index;
	assert(IS_BLOCK(DSF_POSITION(dsf)));

	// Save symbol describing the function (if we called this as the result of
	// a word or path lookup)
	DS_SKIP;
	if (!label) {
		// !!! When a function was not invoked through looking up a word to
		// (or a word in a path) to use as a label, there were three different
		// alternate labels used.  One was SYM__APPLY_, another was
		// ROOT_NONAME, and another was to be the type of the function being
		// executed.  None are fantastic, but we do the type for now.
		label = Get_Type_Word(VAL_TYPE(func));
	} else
		assert(IS_WORD(label));

	*DS_TOP = *label;
	// !!! Not sure why this is needed; seems the label word should be unbound
	// if anything...
	VAL_WORD_FRAME(DS_TOP) = VAL_FUNC_ARGS(func);
	assert(IS_WORD(DSF_LABEL(dsf)));

	// Save FUNC value for safety (spec, args, code):
	DS_SKIP;
	*DS_TOP = *func;
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
		pvs->select = Do_Blk(VAL_SERIES(path), 0);
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
*/	REBVAL *Do_Path(REBVAL **path_val, REBVAL *val)
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
*/	void Pick_Path(REBVAL *value, REBVAL *selector, REBVAL *val)
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
	DS_PUSH_NONE;
	pvs.store = DS_TOP;		// Temp space for constructed results

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
*/	static REBINT Do_Args(REBINT dsf, REBVAL *path, REBSER *block, REBCNT index)
/*
**		Evaluate code block according to the function arg spec.
**		Args are pushed onto the data stack in the same order
**		as the function frame.
**
**			func_offset:  offset of the function or path value, relative to DS_Base
**			path:  refinements or object/function path
**			block: current evaluation block
**			index: current evaluation index
**
***********************************************************************/
{
#if !defined(NDEBUG)
	REBINT dsp_orig;
#endif

	REBVAL *value;
	REBVAL *args;
	REBSER *words;
	REBINT ds = 0;			// stack argument position
	REBINT dsp = DSP + 1;	// stack base
	REBVAL *tos;
	REBVAL *func;

	if ((dsp + 100) > (REBINT)SERIES_REST(DS_Series)) {
		Expand_Stack(STACK_MIN);
	}

	// We can only assign this *after* the stack expansion (may move it)
	func = DSF_FUNC(dsf);

	// Note we must compensate for first arg already pushed if it is an OP
	assert(dsf == DSP - DSF_SIZE - (IS_OP(func) ? 1 : 0));

	// Get list of words:
	words = VAL_FUNC_WORDS(func);
	args = BLK_SKIP(words, 1);
	ds = SERIES_TAIL(words)-1;	// length of stack fill below
	//Debug_Fmt("Args: %z", VAL_FUNC_ARGS(func));

	// If func is operator, first arg is already on stack:
	if (IS_OP(func)) {
		//if (!TYPE_CHECK(args, VAL_TYPE(DS_VALUE(DSP))))
		//	Trap3_DEAD_END(RE_EXPECT_ARG, DSF_LABEL(dsf), args, Of_Type(DS_VALUE(ds)));
		args++;	 	// skip evaluation, but continue with type check
		ds--;		// shorten stack fill below
	}

	// Fill stack variables with default values:
	tos = DS_NEXT;
	DSP += ds;
	for (; ds > 0; ds--) SET_NONE(tos++);

#if !defined(NDEBUG)
	dsp_orig = DSP;
#endif

	// Protocol is to return the last evaluated value on TOS, which may
	// be a THROWN attempt at evaluating an arg.  So our invariant is
	// to keep one value at TOS the whole time.
	// !!! If it looks inefficient to be DS_DROP'ing before a Do_Next to
	// keep the invariant, that's temporary...StableStack fixes it.
	DS_SKIP;

	// The caller will be testing the top slot, and we might not write to
	// it (e.g. what if all params are quoted?) so we have to put something
	// that is *not* THROWN() there.
	SET_TRASH_SAFE(DS_TOP);

	// Go thru the word list args:
	ds = dsp;
	for (; NOT_END(args); args++, ds++) {
		//if (Trace_Flags) Trace_Arg(ds - dsp, args, path);

		// Process each formal argument:
		switch (VAL_TYPE(args)) {

		case REB_WORD:		// WORD - Evaluate next value
			DS_DROP;
			index = Do_Next(block, index, IS_OP(func));
			if (THROWN(DS_TOP)) goto return_index;
			if (index == END_FLAG) Trap2_DEAD_END(RE_NO_ARG, DSF_LABEL(dsf), args);
			DS_Base[ds] = *DS_TOP;
			break;

		case REB_LIT_WORD:	// 'WORD - Just get next value
			if (index < BLK_LEN(block)) {
				value = BLK_SKIP(block, index);
				if (IS_PAREN(value) || IS_GET_WORD(value) || IS_GET_PATH(value)) {
					DS_DROP;
					index = Do_Next(block, index, IS_OP(func));
					if (THROWN(DS_TOP)) goto return_index;
					DS_Base[ds] = *DS_TOP;
				}
				else {
					index++;
					DS_Base[ds] = *value;
				}
			} else
				SET_UNSET(&DS_Base[ds]); // allowed to be none
			break;

		case REB_GET_WORD:	// :WORD - Get value
			if (index < BLK_LEN(block)) {
				DS_Base[ds] = *BLK_SKIP(block, index);
				index++;
			} else
				SET_UNSET(&DS_Base[ds]); // allowed to be none
			break;

		case REB_REFINEMENT: // /WORD - Function refinement
			if (!path || IS_END(path)) return index;
			if (IS_WORD(path)) {
				// Optimize, if the refinement is the next arg:
				if (SAME_SYM(path, args)) {
					SET_TRUE(DS_VALUE(ds)); // set refinement stack value true
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
						SET_TRUE(DS_VALUE(ds)); // set refinement stack value true
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
		if (!TYPE_CHECK(args, VAL_TYPE(DS_VALUE(ds))))
			Trap3_DEAD_END(RE_EXPECT_ARG, DSF_LABEL(dsf), args, Of_Type(DS_VALUE(ds)));
	}

	// Hack to process remaining path:
	if (path && NOT_END(path)) goto more_path;
	//	Trap2_DEAD_END(RE_NO_REFINE, DSF_LABEL(dsf), path);

return_index:
	assert(DSP == dsp_orig + 1);
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
*/	REBCNT Do_Next(REBSER *block, REBCNT index, REBFLG op)
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
#endif

	REBVAL *value;
	REBINT dsf;

	// Functions don't have "names", though they can be assigned to words.
	// If a function invokes via word lookup (vs. a literal FUNCTION! value),
	// 'label' will be that WORD!, and NULL otherwise.
	REBVAL *label = NULL;

	// Most of what this routine does can be done with value pointers and
	// the data stack.  Some operations need a unit of additional storage.
	// This is a one-REBVAL-sized cell for saving that data.
	REBVAL save;

#ifndef NDEBUG
	// This counter is helpful for tracking a specific invocation.
	// If you notice a crash, look on the stack for the topmost call
	// and read the count...then put that here and recompile with
	// a breakpoint set.  (The 'count_static' value is captured into a
	// local 'count' so	you still get the right count after recursion.)

	static int count_static = 0;
	int count = ++count_static;
	if (count == 0 /* don't commit your change! */) {
		VAL_SET(&save, REB_BLOCK);
		VAL_SERIES(&save) = block;
		VAL_INDEX(&save) = index;
		PROBE_MSG(&save, "Do_Next count trap");
	}
#endif

	//CHECK_MEMORY(1);
	CHECK_C_STACK_OVERFLOW(&value);
	if ((DSP + 20) > (REBINT)SERIES_REST(DS_Series)) Expand_Stack(STACK_MIN); //Trap_DEAD_END(RE_STACK_OVERFLOW);
	if (--Eval_Count <= 0 || Eval_Signals) Do_Signals();

	value = BLK_SKIP(block, index);
	//if (Trace_Flags) Trace_Eval(block, index);

	if (Trace_Flags) Trace_Line(block, index, value);

	//getchar();
	switch (EVAL_TYPE(value)) {

	case ET_WORD:
		DS_SKIP;
		GET_VAR_INTO(DS_TOP, value);
		if (IS_UNSET(DS_TOP)) Trap1_DEAD_END(RE_NO_VALUE, value);
		if (ANY_FUNC(DS_TOP)) {
			// OP! is only handled by the code at the tail of this routine
			if (IS_OP(DS_TOP)) Trap_Type_DEAD_END(DS_TOP);

			// We will reuse the TOS for the OUT of the call frame
			label = value;
			value = DS_TOP;
			if (Trace_Flags) Trace_Line(block, index, value);
			goto func_needs_push;
		}
		index++;
		break;

	case ET_SELF:
		DS_PUSH(value);
		index++;
		break;

	case ET_SET_WORD:
		index = Do_Next(block, index+1, 0);
		// THROWN is handled in Set_Var.
		if (index == END_FLAG || VAL_TYPE(DS_TOP) <= REB_UNSET)
			Trap1_DEAD_END(RE_NEED_VALUE, value);
		Set_Var(value, DS_TOP); // evaluation stays on top of stack
		break;

	case ET_FUNCTION:
		DS_SKIP;

	// Value must be the function, and space for the return slot (DSF_OUT)
	// needs to already be accounted for
	func_needs_push:
		assert(ANY_FUNC(value) && (DSP == dsp_orig + 1));
		dsf = Push_Func(block, index, label, value);
		SET_UNSET(DSF_OUT(dsf));

	// 'dsf' holds index of new call frame, not yet set during arg evaluation
	// (because the arguments want to be computed in the caller's environment)
	// value can be invalid at this point, but must be retrievable w/DSF_FUNC
	func_already_pushed:
		assert(IS_UNSET(DSF_OUT(dsf)) && dsf > DSF && !THROWN(DS_TOP));
		index = Do_Args(dsf, 0, block, index+1);

	// The function frame is completely filled with arguments and ready
	func_ready_to_call:
		value = DSF_FUNC(dsf);
		assert(ANY_FUNC(value) && IS_UNSET(DSF_OUT(dsf)) && dsf > DSF);

		if (THROWN(DS_TOP)) {
			// If the result of the Do_Args was something like a RETURN or
			// a BREAK, then nevermind the function we were going to call.
			// Just bubble up that special "ERROR" value to the caller.
			*DSF_OUT(dsf) = *DS_POP;
		}
		else {
			// If the last value Do_Args evaluated wasn't thrown, we don't
			// need to pay attention to it here.
			DS_DROP;

			// The arguments were successfully acquired, so we set the
			// the DSF to our constructed 'dsf' during the Push_Func...then
			// call the function...then put the DSF back to the call level
			// of whoever called us.

			SET_DSF(dsf);
			if (Trace_Flags) Trace_Func(label, value);
			Func_Dispatch[VAL_TYPE(value) - REB_NATIVE](value);

			SET_DSF(PRIOR_DSF(dsf));
		}

		// Drop stack back to where the DSF_OUT(dsf) is now the Top of Stack
		DSP = dsf;

		if (Trace_Flags) Trace_Return(label, DS_TOP);

		// The return value is a FUNC that needs to be re-evaluated.
		if (VAL_GET_OPT(DS_TOP, OPTS_REVAL) && ANY_FUNC(DS_TOP)) {
			value = DS_TOP;

			if (IS_OP(value)) Trap_Type_DEAD_END(value); // not allowed

			label = NULL;
			index--; // Backup block index to re-evaluate.

			// We'll reuse the DS_TOP (where value lives) as the next DS_OUT
			goto func_needs_push;
		}
		break;

	case ET_OPERATOR:
		// Can't actually run an OP! arg unless it's after an evaluation
		Trap1_DEAD_END(RE_NO_OP_ARG, label);

	handle_op:
		assert(DSP > 0 && index != 0);
		// TOS has first arg, we will re-use that slot for the OUT value
		dsf = Push_Func(block, index, label, value);
		DS_PUSH(DSF_OUT(dsf)); // Copy prior to first argument
		SET_UNSET(DSF_OUT(dsf)); // initialize to unset before function call
		goto func_already_pushed;

	case ET_PATH:  // PATH, SET_PATH
		label = value; // a path
		//index++; // now done below with +1

		if (IS_SET_PATH(value)) {
			index = Do_Next(block, index+1, 0);
			// THROWN is handled in Do_Path.
			if (index == END_FLAG || VAL_TYPE(DS_TOP) <= REB_UNSET)
				Trap1_DEAD_END(RE_NEED_VALUE, label);
			Do_Path(&label, DS_TOP);
		}
		else { // Can be a path or get-path:

			// returns in word the path item, DS_TOP has value
			value = Do_Path(&label, 0);

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

				// re-use TOS for OUT of function frame
				dsf = Push_Func(block, index, label, value);

				index = Do_Args(dsf, label + 1, block, index + 1);

				// We now refresh the function value because Do may have moved
				// the stack.  With the function value saved, we default the
				// function output to UNSET!
				value = DSF_FUNC(dsf);
				SET_UNSET(DSF_OUT(dsf));

				goto func_ready_to_call;
			} else
				index++;
		}
		break;

	case ET_PAREN:
		DO_BLK(value);
		DSP++; // keep it on top
		index++;
		break;

	case ET_LIT_WORD:
		DS_PUSH(value);
		VAL_SET(DS_TOP, REB_WORD);
		index++;
		break;

	case ET_GET_WORD:
		DS_SKIP;
		GET_VAR_INTO(DS_TOP, value);
		index++;
		break;

	case ET_LIT_PATH:
		DS_PUSH(value);
		VAL_SET(DS_TOP, REB_PATH);
		index++;
		break;

	case ET_END:
		 return END_FLAG;

	case ET_INVALID:
		 Trap1_DEAD_END(RE_NO_VALUE, value);
		 break;

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

	assert(DSP == dsp_orig + 1);
	return index;
}


/***********************************************************************
**
*/	REBVAL *Do_Blk(REBSER *block, REBCNT index)
/*
**		Evaluate a block from the index position specified.
**		Return the result (a pointer to TOS+1).
**
***********************************************************************/
{
	REBVAL *tos = 0;
	REBINT start = DSP;

	CHECK_MEMORY(4); // Be sure we don't go far with a problem.

	assert(block->info);

	while (index < BLK_LEN(block)) {
		index = Do_Next(block, index, 0);
		tos = DS_POP;
		if (THROWN(tos)) break;
	}
	// If block was empty:
	if (!tos) {
		// CC#2229 - respond to Halt() in code like 'forever []'
		if (--Eval_Count <= 0 || Eval_Signals) Do_Signals();

		tos = DS_NEXT; SET_UNSET(tos);
	}

	if (start != DSP || tos != &DS_Base[start+1]) Trap_DEAD_END(RE_MISSING_ARG);

//	assert(gcd == GC_Disabled, RP_GC_STUCK);

	// Restore data stack and return value:
//	assert((tos == 0 || (start == DSP && tos == &DS_Base[start+1])), RP_TOS_DRIFT);
//	if (!tos) {tos = DS_NEXT; SET_UNSET(tos);}
	return tos;
}


/***********************************************************************
**
*/	REBVAL *Do_Block_Value_Throw(REBVAL *block)
/*
**		A common form of Do_Blk(). Takes block value. Handles throw.
**
***********************************************************************/
{
	REBSER *series = VAL_SERIES(block);
	REBCNT index = VAL_INDEX(block);
	REBVAL *tos = 0;
	REBINT start = DSP;

	while (index < BLK_LEN(series)) {
		index = Do_Next(series, index, 0);
		tos = DS_POP;
		if (THROWN(tos)) Throw(tos, NULL);
	}
	// If series was empty:
	if (!tos) {
		// CC#2229 - respond to Halt() in code like 'forever []'
		if (--Eval_Count <= 0 || Eval_Signals) Do_Signals();

		tos = DS_NEXT; SET_UNSET(tos);
	}

	if (start != DSP || tos != &DS_Base[start+1]) Trap_DEAD_END(RE_MISSING_ARG);

	return tos;
}


/***********************************************************************
**
*/	void Reduce_Block(REBSER *block, REBCNT index, REBVAL *into)
/*
**		Reduce block from the index position specified in the value.
**		Collect all values from stack and make them a block.
**
***********************************************************************/
{
	REBCNT len = 0;
	REBSER *ser = NULL;
	REBVAL blk;
	enum REBOL_Types type;

	if (into != NULL) {
		ser = VAL_SERIES(into);
		type = VAL_TYPE(into);
		len = VAL_INDEX(into) + SERIES_LEN(block) - index;
	} else {
		ser = Make_Block(SERIES_TAIL(block) - index);
		if (ser == NULL)
			Panic(RE_NO_MEMORY);
		type = REB_BLOCK;
		len = 0;
	}

	VAL_SET(&blk, type);
	VAL_SERIES(&blk) = ser;
	VAL_INDEX(&blk) = len;
	DS_PUSH(&blk); //push here avoid the blk being GC'ed later

	while (index < BLK_LEN(block)) {
		index = Do_Next(block, index, 0);
		if (THROWN(DS_TOP)) return;
		Append_Value(ser, DS_POP);
	}
}


/***********************************************************************
**
*/	void Reduce_Only(REBSER *block, REBCNT index, REBVAL *words, REBVAL *into)
/*
**		Reduce only words and paths not found in word list.
**
***********************************************************************/
{
	REBINT start = DSP + 1;
	REBVAL *val;
	REBSER *ser = 0;
	REBCNT idx = 0;

	REBCNT len = 0;
	REBSER *dest_ser = NULL;
	REBVAL blk;
	enum REBOL_Types type;

	if (into != NULL) {
		dest_ser = VAL_SERIES(into);
		type = VAL_TYPE(into);
		len = VAL_INDEX(into) + SERIES_LEN(block) - index;
	} else {
		dest_ser = Make_Block(SERIES_LEN(block) - index);
		if (dest_ser == NULL) Panic(RE_NO_MEMORY);
		type = REB_BLOCK;
		len = 0;
	}

	VAL_SET(&blk, type);
	VAL_SERIES(&blk) = dest_ser;
	VAL_INDEX(&blk) = len;
	DS_PUSH(&blk); //push here avoid the blk being GC'ed later

	if (IS_BLOCK(words)) {
		ser = VAL_SERIES(words);
		idx = VAL_INDEX(words);
	}

	for (val = BLK_SKIP(block, index); NOT_END(val); val++) {
		if (IS_WORD(val)) {
			const REBVAL *v;
			// Check for keyword:
			if (ser && NOT_FOUND != Find_Word(ser, idx, VAL_WORD_CANON(val))) {
				Append_Value(dest_ser, val);
				continue;
			}
			v = GET_VAR(val);
			Append_Value(dest_ser, v);
		}
		else if (IS_PATH(val)) {
			REBVAL *v;
			if (ser) {
				// Check for keyword/path:
				v = VAL_BLK_DATA(val);
				if (IS_WORD(v)) {
					if (NOT_FOUND != Find_Word(ser, idx, VAL_WORD_CANON(v))) {
						Append_Value(dest_ser, val);
						continue;
					}
				}
			}
			v = val;
			Do_Path(&v, 0); // pushes val on stack
			Append_Value(dest_ser, DS_POP);
		}
		else Append_Value(dest_ser, val);
		// No need to check for unwinds (THROWN) here, because unwinds should
		// never be accessible via words or paths.
	}
}


/***********************************************************************
**
*/	void Reduce_Block_No_Set(REBSER *block, REBCNT index, REBVAL *into)
/*
***********************************************************************/
{
	REBCNT len = 0;
	REBSER *ser = NULL;
	REBVAL blk;
	REBVAL *val = NULL;
	enum REBOL_Types type;

	if (into != NULL) {
		ser = VAL_SERIES(into);
		type = VAL_TYPE(into);
		len = VAL_INDEX(into) + SERIES_LEN(block) - index;
	} else {
		ser = Make_Block(SERIES_LEN(block) - index);
		if (ser == NULL) Panic(RE_NO_MEMORY);
		type = REB_BLOCK;
		len = 0;
	}

	VAL_SET(&blk, type);
	VAL_SERIES(&blk) = ser;
	VAL_INDEX(&blk) = len;
	DS_PUSH(&blk); //push here avoid the blk being GC'ed later

	while (index < BLK_LEN(block)) {
		if (IS_SET_WORD(val = BLK_SKIP(block, index))) {
			DS_PUSH(val);
			index++;
		} else
			index = Do_Next(block, index, 0);
		if (THROWN(DS_TOP)) return;
		Append_Value(ser, DS_POP);
	}

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
			REBVAL *v = val;
			if (!Do_Path(&v, 0)) { // pushes val on stack
				if (VAL_TYPE(DS_TOP) != type) DS_DROP;
			}
		}
		else if (VAL_TYPE(val) == type) DS_PUSH(val);
		// !!! check stack size
	}
	SET_END(&DS_Base[++DSP]); // in case caller needs it

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
*/	void Compose_Block(REBVAL *block, REBFLG deep, REBFLG only, REBVAL *into)
/*
**		Compose a block from a block of un-evaluated values and
**		paren blocks that are evaluated. Stack holds temp values,
**		which also protects them from GC along the way.
**
**			deep - recurse into sub-blocks
**			only - parens that return blocks are kept as blocks
**
**		Returns result as a block on top of stack.
**
***********************************************************************/
{
	REBVAL *value;
	REBINT start = DSP + 1;
	REBCNT len = 0;
	REBINT needs_free = 0;
	REBSER *ser = NULL;
	REBVAL blk;
	enum REBOL_Types type;

	if (into != NULL) {
		ser = VAL_SERIES(into);
		type = VAL_TYPE(into);
		len = VAL_INDEX(into) + VAL_BLK_LEN(block);
	} else {
		ser = Make_Block(VAL_BLK_LEN(block));
		if (ser == NULL) Panic(RE_NO_MEMORY);
		type = REB_BLOCK;
		len = 0;
		needs_free = 1;
	}

	VAL_SET(&blk, type);
	VAL_SERIES(&blk) = ser;
	VAL_INDEX(&blk) = len;
	DS_PUSH(&blk); //push here avoid the blk being GC'ed later

	for (value = VAL_BLK_DATA(block); NOT_END(value); value++) {
		if (IS_PAREN(value)) {
			// Eval the paren, and leave result on the stack:
			REBVAL *paren = DO_BLK(value);
			if (THROWN(paren)) {
				if (needs_free) Free_Series(ser);
				DSP ++;
				return;
			}

			// If result is a block, and not /only, insert its contents:
			if (IS_BLOCK(paren) && !only) {
				// Append series:
				Append_Series(ser, (REBYTE *)VAL_BLK_DATA(paren), VAL_BLK_LEN(paren));
			}
			else if (!IS_UNSET(paren)) Append_Value(ser, paren); //don't append result if unset is returned
		}
		else if (deep) {
			if (IS_BLOCK(value)) {
				Compose_Block(value, TRUE, only, 0);
				Append_Value(ser, DS_POP);
			}
			else {
				REBVAL tmp = *value;
				if (ANY_BLOCK(value)) // Include PATHS
					VAL_SERIES(&tmp) = Copy_Block(VAL_SERIES(value), 0);
				Append_Value(ser, &tmp);
			}
		}
		else {
			Append_Value(ser, value);
		}
	}

}


/***********************************************************************
**
*/	void Apply_Block(REBVAL *func, REBVAL *args, REBFLG reduce)
/*
**		Result is on top of stack.
**
***********************************************************************/
{
	REBINT ftype = VAL_TYPE(func) - REB_NATIVE; // function type
	REBSER *block = VAL_SERIES(args);
	REBCNT index = VAL_INDEX(args);
	REBCNT dsf;

	REBSER *words;
	REBINT len;
	REBINT n;
	REBINT start;
	REBVAL *val;

	if (index > SERIES_TAIL(block)) index = SERIES_TAIL(block);

	// Push function frame:
	DS_PUSH_UNSET; // OUT slot for function eval result
	dsf = Push_Func(block, index, NULL, func);
	func = DSF_FUNC(dsf); // for safety

	// Determine total number of args:
	words = VAL_FUNC_WORDS(func);
	len = words ? SERIES_TAIL(words)-1 : 0;
	start = DSP+1;

	// Gather arguments:
	if (reduce) {
		// Reduce block contents to stack:
		n = 0;
		while (index < BLK_LEN(block)) {
			index = Do_Next(block, index, 0);
			if (THROWN(DS_TOP)) return;
			n++;
		}
		if (n > len) DSP = start + len;
	}
	else {
		// Copy block contents to stack:
		n = VAL_BLK_LEN(args);
		if (len < n) n = len;
		if (start + n + 100 > cast(REBINT, SERIES_REST(DS_Series)))
			Expand_Stack(STACK_MIN);
		memcpy(&DS_Base[start], BLK_SKIP(block, index), n * sizeof(REBVAL));
		DSP = start + n - 1;
	}

	// Pad out missing args:
	for (; n < len; n++) DS_PUSH_NONE;

	// Validate arguments:
	if (words) {
		val = DS_Base + start;
		for (args = BLK_SKIP(words, 1); NOT_END(args);) {
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
	SET_DSF(dsf);
	Func_Dispatch[ftype](func);
	DSP = dsf;
	SET_DSF(PRIOR_DSF(dsf));
}


/***********************************************************************
**
*/	void Apply_Function(REBVAL *func, va_list *args)
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
	REBCNT dsf;
	REBSER *words;
	REBCNT ds;
	REBVAL *arg;

	REBSER *wblk; // where block (where we were called)
	REBCNT widx; // where index (position in above block)

	DS_PUSH_UNSET; // OUT slot for function eval result

	// For debugging purposes, DO wants to know what our execution
	// block and position are.  We have to make something up, because
	// this call is originating from C code (not Rebol code).
	if (DSF != 0) {
		// Some function is on the stack, so fabricate our execution
		// position by copying the block and position it was at.

		wblk = VAL_SERIES(DSF_POSITION(DSF));
		widx = VAL_INDEX(DSF_POSITION(DSF));
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

	dsf = Push_Func(wblk, widx, NULL, func);
	func = DSF_FUNC(dsf); // for safety
	words = VAL_FUNC_WORDS(func);
	ds = SERIES_TAIL(words)-1;	// length of stack fill below
	if (DSP + ds + 100 > SERIES_REST(DS_Series)) {//unlikely
		Expand_Stack(STACK_MIN);
		func = DSF_FUNC(dsf); //reevaluate func
	}

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
	DSP = dsf;
}


/***********************************************************************
**
*/	void Apply_Func(REBSER *where, REBVAL *func, ...)
/*
**		Applies function from args provided by C call. Zero terminated.
**		Return value is on TOS
**
***********************************************************************/
{
	va_list args;

	if (!ANY_FUNC(func)) Trap_Arg(func);

	va_start(args, func);
	Apply_Function(func, &args);
	va_end(args);
}


/***********************************************************************
**
*/	void Do_Sys_Func(REBCNT inum, ...)
/*
**		Evaluates a SYS function and TOS1 contains
**		the result (VOLATILE). Uses current stack frame location
**		as the next location (e.g. for error output).
**
***********************************************************************/
{
	va_list args;
	REBVAL *value = FRM_VALUE(Sys_Context, inum);

	if (!ANY_FUNC(value)) Trap1(RE_BAD_SYS_FUNC, value);

	va_start(args, inum);
	Apply_Function(value, &args);
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
*/	REBVAL *Do_Bind_Block(REBSER *frame, REBVAL *block)
/*
**		Bind deep and evaluate a block value in a given context.
**		Result is left on top of data stack (may be an error).
**
***********************************************************************/
{
	Bind_Block(frame, VAL_BLK_DATA(block), BIND_DEEP);
	return DO_BLK(block);
}


/***********************************************************************
**
*/	void Reduce_Bind_Block(REBSER *frame, REBVAL *block, REBCNT binding)
/*
**		Bind deep and reduce a block value in a given context.
**		Result is left on top of data stack (may be an error).
**
***********************************************************************/
{
	Bind_Block(frame, VAL_BLK_DATA(block), binding);
	Reduce_Block(VAL_SERIES(block), VAL_INDEX(block), 0);
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
	VAL_WORD_FRAME(DSF_LABEL(DSF)) = VAL_FUNC_ARGS(func_val);
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
**	Return:
**		On return, the stack remains as-is. The caller must reset
**		the DSP and DSF values.
**
***********************************************************************/
{
	REBSER *wsrc;		// words of source func
	REBSER *wnew;		// words of target func
	REBCNT isrc;		// index position in source frame
	REBCNT inew;		// index position in target frame
	REBVAL *word;
	REBVAL *word2;

	//!!! NEEDS to check stack for overflow
	//!!! Should check datatypes for new arg passing!

	wsrc = VAL_FUNC_WORDS(DSF_FUNC(DSF));
	wnew = VAL_FUNC_WORDS(func_val);

	// Foreach arg of the target, copy to source until refinement.
	for (isrc = inew = 1; inew < BLK_LEN(wnew); inew++, isrc++) {
		word = BLK_SKIP(wnew, inew);
		if (isrc > BLK_LEN(wsrc)) isrc = BLK_LEN(wsrc);

		switch (VAL_TYPE(word)) {
			case REB_WORD:
			case REB_LIT_WORD:
			case REB_GET_WORD:
				if (VAL_TYPE(word) == VAL_TYPE(BLK_SKIP(wsrc, isrc))) break;
				DS_PUSH_NONE;
				continue;
				//Trap_Arg_DEAD_END(word);

			// At refinement, search for it in source, then continue with words.
			case REB_REFINEMENT:
				// Are we aligned on the refinement already? (a common case)
				word2 = BLK_SKIP(wsrc, isrc);
				if (!(IS_REFINEMENT(word2) && VAL_BIND_CANON(word2) == VAL_BIND_CANON(word))) {
					// No, we need to search for it:
					for (isrc = 1; isrc < BLK_LEN(wsrc); isrc++) {
						word2 = BLK_SKIP(wsrc, isrc);
						if (IS_REFINEMENT(word2) && VAL_BIND_CANON(word2) == VAL_BIND_CANON(word)) goto push_arg;
					}
					DS_PUSH_NONE;
					continue;
					//if (isrc >= BLK_LEN(wsrc)) Trap_Arg_DEAD_END(word);
				}
				break;

			default:
				assert(FALSE);
		}
push_arg:
		DS_PUSH(DSF_ARG(DSF, isrc));
		//Debug_Fmt("Arg %d -> %d", isrc, inew);
	}

	// Copy values to prior location:
	inew--;
	// memory areas may overlap, so use memmove and not memcpy!
	memmove(DS_ARG(1), DS_TOP-(inew-1), inew * sizeof(REBVAL));
	DSP = DS_ARG_BASE + inew; // new TOS
	//Dump_Block(DS_ARG(1), inew);
	VAL_WORD_FRAME(DSF_LABEL(DSF)) = VAL_FUNC_ARGS(func_val);
	*DSF_FUNC(DSF) = *func_val;
	Func_Dispatch[VAL_TYPE(func_val)-REB_NATIVE](func_val);
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
		REBVAL *v = &path;
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
