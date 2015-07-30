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
**  Module:  n-control.c
**  Summary: native functions for control flow
**  Section: natives
**  Author:  Carl Sassenrath
**  Notes:
**    Warning: Do not cache pointer to stack ARGS (stack may expand).
**
***********************************************************************/

#include "sys-core.h"


// Local flags used for Protect functions below:
enum {
	PROT_SET,
	PROT_DEEP,
	PROT_HIDE,
	PROT_WORD,
	PROT_MAX
};


/***********************************************************************
**
*/	void Protected(REBVAL *word)
/*
**		Throw an error if word is protected.
**
***********************************************************************/
{
	REBSER *frm;
	REBINT index = VAL_WORD_INDEX(word);

	if (index > 0) {
		frm = VAL_WORD_FRAME(word);
		if (VAL_PROTECTED(FRM_WORDS(frm)+index))
			Trap1(RE_LOCKED_WORD, word);
	}
	else if (index == 0) Trap(RE_SELF_PROTECTED);
}


/***********************************************************************
**
*/	static void Protect_Word(REBVAL *value, REBCNT flags)
/*
***********************************************************************/
{
	if (GET_FLAG(flags, PROT_WORD)) {
		if (GET_FLAG(flags, PROT_SET)) VAL_SET_OPT(value, OPTS_LOCK);
		else VAL_CLR_OPT(value, OPTS_LOCK);
	}

	if (GET_FLAG(flags, PROT_HIDE)) {
		if GET_FLAG(flags, PROT_SET) VAL_SET_OPT(value, OPTS_HIDE);
		else VAL_CLR_OPT(value, OPTS_HIDE);
	}
}


/***********************************************************************
**
*/	static void Protect_Value(REBVAL *value, REBCNT flags)
/*
**		Anything that calls this must call Unmark() when done.
**
***********************************************************************/
{
	if (ANY_SERIES(value) || IS_MAP(value))
		Protect_Series(value, flags);
	else if (IS_OBJECT(value) || IS_MODULE(value))
		Protect_Object(value, flags);
}


/***********************************************************************
**
*/	void Protect_Series(REBVAL *val, REBCNT flags)
/*
**		Anything that calls this must call Unmark() when done.
**
***********************************************************************/
{
	REBSER *series = VAL_SERIES(val);

	if (SERIES_GET_FLAG(series, SER_MARK)) return; // avoid loop

	if (GET_FLAG(flags, PROT_SET))
		PROTECT_SERIES(series);
	else
		UNPROTECT_SERIES(series);

	if (!ANY_BLOCK(val) || !GET_FLAG(flags, PROT_DEEP)) return;

	SERIES_SET_FLAG(series, SER_MARK); // recursion protection

	for (val = VAL_BLK_DATA(val); NOT_END(val); val++) {
		Protect_Value(val, flags);
	}
}


/***********************************************************************
**
*/	void Protect_Object(REBVAL *value, REBCNT flags)
/*
**		Anything that calls this must call Unmark() when done.
**
***********************************************************************/
{
	REBSER *series = VAL_OBJ_FRAME(value);

	if (SERIES_GET_FLAG(series, SER_MARK)) return; // avoid loop

	if (GET_FLAG(flags, PROT_SET)) PROTECT_SERIES(series);
	else UNPROTECT_SERIES(series);

	for (value = FRM_WORDS(series)+1; NOT_END(value); value++) {
		Protect_Word(value, flags);
	}

	if (!GET_FLAG(flags, PROT_DEEP)) return;

	SERIES_SET_FLAG(series, SER_MARK); // recursion protection

	for (value = FRM_VALUES(series)+1; NOT_END(value); value++) {
		Protect_Value(value, flags);
	}
}


/***********************************************************************
**
*/	static void Protect_Word_Value(REBVAL *word, REBCNT flags)
/*
***********************************************************************/
{
	REBVAL *wrd;
	REBVAL *val;

	if (ANY_WORD(word) && HAS_FRAME(word) && VAL_WORD_INDEX(word) > 0) {
		wrd = FRM_WORDS(VAL_WORD_FRAME(word))+VAL_WORD_INDEX(word);
		Protect_Word(wrd, flags);
		if (GET_FLAG(flags, PROT_DEEP)) {
			// Ignore existing mutability state, by casting away the const.
			// (Most routines should DEFINITELY not do this!)
			val = m_cast(REBVAL*, GET_VAR(word));
			Protect_Value(val, flags);
			Unmark(val);
		}
	}
	else if (ANY_PATH(word)) {
		REBCNT index;
		REBSER *obj;
		if ((obj = Resolve_Path(word, &index))) {
			wrd = FRM_WORD(obj, index);
			Protect_Word(wrd, flags);
			if (GET_FLAG(flags, PROT_DEEP)) {
				Protect_Value(val = FRM_VALUE(obj, index), flags);
				Unmark(val);
			}
		}
	}
}


/***********************************************************************
**
*/	static int Protect(REBVAL *ds, REBCNT flags)
/*
**		1: value
**		2: /deep  - recursive
**		3: /words  - list of words
**		4: /values - list of values
**		5: /hide  - hide variables
**
***********************************************************************/
{
	REBVAL *val = D_ARG(1);

	// flags has PROT_SET bit (set or not)

	Check_Security(SYM_PROTECT, POL_WRITE, val);

	if (D_REF(2)) SET_FLAG(flags, PROT_DEEP);
	//if (D_REF(3)) SET_FLAG(flags, PROT_WORD);

	if (D_REF(5)) SET_FLAG(flags, PROT_HIDE);
	else SET_FLAG(flags, PROT_WORD); // there is no unhide

	if (IS_WORD(val) || IS_PATH(val)) {
		Protect_Word_Value(val, flags); // will unmark if deep
		return R_ARG1;
	}

	if (IS_BLOCK(val)) {
		if (D_REF(3)) { // /words
			for (val = VAL_BLK_DATA(val); NOT_END(val); val++)
				Protect_Word_Value(val, flags);  // will unmark if deep
			return R_ARG1;
		}
		if (D_REF(4)) { // /values
			REBVAL *val2;
			for (val = VAL_BLK_DATA(val); NOT_END(val); val++) {
				if (IS_WORD(val)) {
					// !!! Temporary and ugly cast; since we *are* PROTECT
					// we allow ourselves to get mutable references to even
					// protected values so we can no-op protect them.
					val2 = m_cast(REBVAL*, GET_VAR(val));
				}
				else if (IS_PATH(val)) {
					REBVAL *path = val;
					if (Do_Path(&path, 0)) {
						val2 = val; // !!! comment said "found a function"
					} else {
						val2 = DS_TOP; // !!! unstable on top of stack
					}
				}
				else
					val2 = val;

				Protect_Value(val2, flags);
				if (GET_FLAG(flags, PROT_DEEP)) Unmark(val2);
			}
			return R_ARG1;
		}
	}

	if (GET_FLAG(flags, PROT_HIDE)) Trap_DEAD_END(RE_BAD_REFINES);

	Protect_Value(val, flags);

	if (GET_FLAG(flags, PROT_DEEP)) Unmark(val);

	return R_ARG1;
}


/***********************************************************************
**
*/	REBNATIVE(also)
/*
***********************************************************************/
{
	return R_ARG1;
}


/***********************************************************************
**
*/	REBNATIVE(all)
/*
***********************************************************************/
{
	REBSER *block = VAL_SERIES(D_ARG(1));
	REBCNT index = VAL_INDEX(D_ARG(1));

	ds = 0;
	while (index < SERIES_TAIL(block)) {
		index = Do_Next(block, index, 0); // stack volatile
		ds = DS_POP;  // volatile stack reference
		if (IS_CONDITIONAL_FALSE(ds)) return R_NONE;
		if (THROWN(ds)) break;
	}
	if (ds == 0) return R_TRUE;
	return R_TOS1;
}


/***********************************************************************
**
*/	REBNATIVE(any)
/*
***********************************************************************/
{
	REBSER *block = VAL_SERIES(D_ARG(1));
	REBCNT index = VAL_INDEX(D_ARG(1));

	while (index < SERIES_TAIL(block)) {
		index = Do_Next(block, index, 0); // stack volatile
		ds = DS_POP;  // volatile stack reference
		if (!IS_CONDITIONAL_FALSE(ds) && !IS_UNSET(ds)) return R_TOS1;
	}
	return R_NONE;
}


/***********************************************************************
**
*/	REBNATIVE(apply)
/*
***********************************************************************/
{
	Apply_Block(D_ARG(1), D_ARG(2), !D_REF(3)); // stack volatile
	return R_TOS;
}


/***********************************************************************
**
*/	REBNATIVE(attempt)
/*
***********************************************************************/
{
	REBOL_STATE state;
	const REBVAL *error;

	PUSH_CATCH(&error, &state);

// The first time through the following code 'error' will be NULL, but...
// Throw() can longjmp here, so 'error' won't be NULL *if* that happens!

	if (error) return R_NONE;

	Do_Blk(VAL_SERIES(D_ARG(1)), VAL_INDEX(D_ARG(1)));
	DSP++; // TOS semantics instead of TOS1!

	DROP_CATCH_SAME_STACKLEVEL_AS_PUSH(&state);

	return R_TOS;
}


/***********************************************************************
**
*/	REBNATIVE(break)
/*
**		1: /return
**		2: value
**
***********************************************************************/
{
	REBVAL *value = D_REF(1) ? D_ARG(2) : UNSET_VALUE;

	VAL_SET(D_OUT, REB_ERROR);
	VAL_ERR_NUM(D_OUT) = RE_BREAK;
	ADD_THROWN_ARG(D_OUT, value);

	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(case)
/*
***********************************************************************/
{
	REBSER *block = VAL_SERIES(D_ARG(1));
	REBCNT index = VAL_INDEX(D_ARG(1));
	REBFLG all_flag = D_REF(2);

	while (index < SERIES_TAIL(block)) {
		//DSP = top; // reset stack  -- not needed?
		index = Do_Next(block, index, 0);
		ds = DS_POP;  // volatile stack reference
		if (IS_CONDITIONAL_FALSE(ds)) index++;
		else {
			if (IS_UNSET(ds)) Trap_DEAD_END(RE_NO_RETURN);
			if (THROWN(ds)) return R_TOS1;
			if (index >= SERIES_TAIL(block)) return R_TRUE;
			index = Do_Next(block, index, 0);
			ds = DS_POP;  // volatile stack reference
			if (IS_BLOCK(ds)) {
				ds = DO_BLK(ds);
				if (IS_UNSET(ds) && !all_flag) return R_TRUE;
			}
			if (THROWN(ds) || !all_flag || index >= SERIES_TAIL(block))
				return R_TOS1;
		}
	}
	return R_NONE;
}


/***********************************************************************
**
*/	REBNATIVE(catch)
/*
***********************************************************************/
{
	REBVAL *val;
	REBCNT sym;
	REBOOL catch_quit = D_REF(4); // Should we catch QUIT too?

	REBOL_STATE state;
	const REBVAL *error;

	PUSH_CATCH_ANY(&error, &state);

// The first time through the following code 'error' will be NULL, but...
// Throw() can longjmp here, so 'error' won't be NULL *if* that happens!

	if (error) {
		// We don't ever want to catch HALT from inside a native; re-throw
		if (VAL_ERR_NUM(error) == RE_HALT) {
			Throw(error, NULL);
			DEAD_END;
		}

		if (VAL_ERR_NUM(error) == RE_QUIT) {
			// If they didn't want to catch quits then re-throw
			if (!catch_quit) {
				Throw(error, NULL);
				DEAD_END;
			}

			// Otherwise, extract the exit status.
			// !!! How does CATCH/QUIT know it caught a QUIT?
			assert(IS_TRASH(TASK_THROWN_ARG));
			SET_INTEGER(D_OUT, VAL_ERR_STATUS(error));
			return R_OUT;
		}

		*D_OUT = *error;
		return R_OUT;
	}

	// Evaluate the block:
	Do_Blk(VAL_SERIES(D_ARG(1)), VAL_INDEX(D_ARG(1)));
	DSP++; // Use TOS semantics, not TOS1

	DROP_CATCH_SAME_STACKLEVEL_AS_PUSH(&state);

	// If it is a throw, process it:
	if (IS_ERROR(DS_TOP) && VAL_ERR_NUM(DS_TOP) == RE_THROW) {

		// If a named throw, then check it:
		if (D_REF(2)) { // /name

			sym = VAL_ERR_SYM(DS_TOP);
			val = D_ARG(3); // name symbol

			// If name is the same word:
			if (IS_WORD(val) && sym == VAL_WORD_CANON(val)) goto got_err;

			// If it is a block of words:
			else if (IS_BLOCK(val)) {
				for (val = VAL_BLK_DATA(val); NOT_END(val); val++) {
					if (IS_WORD(val) && sym == VAL_WORD_CANON(val))
						goto got_err;
				}
			}
		} else {
got_err:
			TAKE_THROWN_ARG(DS_TOP, DS_TOP);
		}
	}

	return R_TOS;
}


/***********************************************************************
**
*/	REBNATIVE(throw)
/*
***********************************************************************/
{
	VAL_SET(D_OUT, REB_ERROR);
	VAL_ERR_NUM(D_OUT) = RE_THROW;
	if (D_REF(2)) // /name
		VAL_ERR_SYM(D_OUT) = VAL_WORD_SYM(D_ARG(3));
	else
		VAL_ERR_SYM(D_OUT) = SYM_NOT_USED;
	ADD_THROWN_ARG(D_OUT, D_ARG(1));

	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(comment)
/*
***********************************************************************/
{
	return R_UNSET;
}


/***********************************************************************
**
*/	REBNATIVE(compose)
/*
**		{Evaluates a block of expressions, only evaluating parens, and returns a block.}
**		value "Block to compose"
**		/deep "Compose nested blocks"
**		/only "Inserts a block value as a block"
**
***********************************************************************/
{
	REBVAL *value = D_ARG(1);

	if (!IS_BLOCK(value)) return R_ARG1;
	Compose_Block(value, D_REF(2), D_REF(3), D_REF(4) ? D_ARG(5) : 0);
	return R_TOS;
}


/***********************************************************************
**
*/	REBNATIVE(continue)
/*
***********************************************************************/
{
	VAL_SET(D_OUT, REB_ERROR);
	VAL_ERR_NUM(D_OUT) = RE_CONTINUE;

	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(do)
/*
***********************************************************************/
{
	REBVAL *value = D_ARG(1);

	switch (VAL_TYPE(value)) {

	case REB_BLOCK:
	case REB_PAREN:
		if (D_REF(4)) { // next
			VAL_INDEX(value) = Do_Next(VAL_SERIES(value), VAL_INDEX(value), 0);
			if (VAL_INDEX(value) == END_FLAG) {
				VAL_INDEX(value) = VAL_TAIL(value);
				Set_Var(D_ARG(5), value);
				return R_UNSET;
			}
			Set_Var(D_ARG(5), value); // "continuation" of block
			return R_TOS;
		}
		else DO_BLK(value);
		return R_TOS1;

    case REB_NATIVE:
	case REB_ACTION:
    case REB_COMMAND:
    case REB_REBCODE:
    case REB_OP:
    case REB_CLOSURE:
	case REB_FUNCTION:
		VAL_SET_OPT(value, OPTS_REVAL);
		return R_ARG1;

//	case REB_PATH:  ? is it used?

	case REB_WORD:
	case REB_GET_WORD:
		GET_VAR_INTO(D_OUT, value);
		return R_OUT;

	case REB_LIT_WORD:
		*D_OUT = *value;
		SET_TYPE(D_OUT, REB_WORD);
		return R_OUT;

	case REB_LIT_PATH:
		*D_OUT = *value;
		SET_TYPE(D_OUT, REB_PATH);
		return R_OUT;

	case REB_ERROR:
		if (IS_THROW(value)) {
			// @HostileFork wants to know if this happens.  It shouldn't
			// (but there was code here that seemed to think it could)
			assert(FALSE);
			return R_ARG1;
		}
		Throw(value, NULL);

	case REB_BINARY:
	case REB_STRING:
	case REB_URL:
	case REB_FILE:
		// DO native and system/intrinsic/do must use same arg list:
		Do_Sys_Func(SYS_CTX_DO_P, value, D_ARG(2), D_ARG(3), D_ARG(4), D_ARG(5), NULL);
		return R_TOS;

	case REB_TASK:
		Do_Task(value);
		return R_ARG1;

	case REB_SET_WORD:
	case REB_SET_PATH:
		Trap_Arg_DEAD_END(value);

	default:
		return R_ARG1;
	}
}


/***********************************************************************
**
*/	REBNATIVE(either)
/*
***********************************************************************/
{
	REBCNT argnum = IS_CONDITIONAL_FALSE(D_ARG(1)) ? 3 : 2;

	if (IS_BLOCK(D_ARG(argnum)) && !D_REF(4) /* not using /ONLY */) {
		DO_BLK(D_ARG(argnum));
		return R_TOS1;
	} else {
		return argnum == 2 ? R_ARG2 : R_ARG3;
	}
}


/***********************************************************************
**
*/	REBNATIVE(exit)
/*
***********************************************************************/
{
	VAL_SET(D_OUT, REB_ERROR);
	VAL_ERR_NUM(D_OUT) = RE_RETURN;
	ADD_THROWN_ARG(D_OUT, UNSET_VALUE);

	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(if)
/*
***********************************************************************/
{
	if (IS_CONDITIONAL_FALSE(D_ARG(1))) return R_NONE;
	if (IS_BLOCK(D_ARG(2)) && !D_REF(3) /* not using /ONLY */) {
		DO_BLK(D_ARG(2));
		return R_TOS1;
	}
	return R_ARG2;
}


/***********************************************************************
**
*/	REBNATIVE(protect)
/*
***********************************************************************/
{
	return Protect(ds, 1); // PROT_SET
}


/***********************************************************************
**
*/	REBNATIVE(unprotect)
/*
***********************************************************************/
{
	SET_NONE(D_ARG(5)); // necessary, bogus, but no harm to stack
	return Protect(ds, 0);
}


/***********************************************************************
**
*/	REBNATIVE(reduce)
/*
***********************************************************************/
{
	if (IS_BLOCK(D_ARG(1))) {
		REBSER *ser = VAL_SERIES(D_ARG(1));
		REBCNT index = VAL_INDEX(D_ARG(1));
		REBVAL *val = D_REF(5) ? D_ARG(6) : 0;

		if (D_REF(2))
			Reduce_Block_No_Set(ser, index, val);
		else if (D_REF(3))
			Reduce_Only(ser, index, D_ARG(4), val);
		else
			Reduce_Block(ser, index, val);
		return R_TOS;
	}

	return R_ARG1;
}


/***********************************************************************
**
*/	REBNATIVE(return)
/*
**		Returns a value from the current function. This is done by
**		returning a special "error!" which indicates a return, and
**		putting the returned value into an associated task-local
**		variable (only one of these is in effect at a time).
**
***********************************************************************/
{
	REBVAL *arg = D_ARG(1);

	VAL_SET(D_OUT, REB_ERROR);
	VAL_ERR_NUM(D_OUT) = RE_RETURN;
	ADD_THROWN_ARG(D_OUT, arg);

	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(switch)
/*
**		value
**		cases [block!]
**		/default
**		case
**      /all {Check all cases}
**
***********************************************************************/
{
	REBVAL *blk = VAL_BLK_DATA(D_ARG(2));
	REBVAL *result;
	REBOOL all = D_REF(5);
	REBOOL found = FALSE;

	// Find value in case block...
	for (; NOT_END(blk); blk++) {
		if (!IS_BLOCK(blk) && 0 == Cmp_Value(DS_ARG(1), blk, FALSE)) { // avoid stack move
			// Skip forward to block...
			for (; !IS_BLOCK(blk) && NOT_END(blk); blk++);
			if (IS_END(blk)) break;
			found = TRUE;
			// Evaluate the case block
			result = DO_BLK(blk);
			if (!all) return R_TOS1;
			if (THROWN(result) && Check_Error(result) >= 0) break;
		}
	}

	if (!found && IS_BLOCK(result = D_ARG(4))) {
		DO_BLK(result);
		return R_TOS1;
	}

	return R_NONE;
}


/***********************************************************************
**
*/	REBNATIVE(try)
/*
**		1: block
**		2: /except
**		3: code
**
***********************************************************************/
{
	REBFLG except = D_REF(2);
	REBVAL handler = *D_ARG(3); // TRY exception will trim the stack

	REBOL_STATE state;
	const REBVAL *error;

	PUSH_CATCH(&error, &state);

// The first time through the following code 'error' will be NULL, but...
// Throw() can longjmp here, so 'error' won't be NULL *if* that happens!

	if (error) {
		if (except) {
			if (IS_BLOCK(D_ARG(3))) {
				// forget the result of the try.
				Do_Blk(VAL_SERIES(D_ARG(3)), VAL_INDEX(D_ARG(3)));
				DSP++; // TOS semantics, not TOS1!
				return R_TOS;
			}
			else if (ANY_FUNC(D_ARG(3))) {
				// !!! REVIEW: What about zero arity functions or functions of
				// arity greater than 1?  What about a function that has
				// more args via refinements but can still act as an
				// arity one function without those refinements?

				REBVAL *args = BLK_SKIP(VAL_FUNC_ARGS(&handler), 1);
				if (NOT_END(args) && !TYPE_CHECK(args, VAL_TYPE(error))) {
					// TODO: This results in an error message such as "action!
					// does not allow error! for its value1 argument". A better
					// message would be more like "except handler does not
					// allow error! for its value1 argument."
					Trap3_DEAD_END(RE_EXPECT_ARG, Of_Type(&handler), args, Of_Type(error));
				}
				Apply_Func(NULL, &handler, error, NULL);
				return R_TOS;
			}
			else
				Panic(RP_MISC); // should not be possible (type-checking)

			DEAD_END;
		}

		*D_OUT = *error;
		return R_OUT;
	}

	Do_Blk(VAL_SERIES(D_ARG(1)), VAL_INDEX(D_ARG(1)));
	DSP++; // TOS semantics, not TOS1!

	DROP_CATCH_SAME_STACKLEVEL_AS_PUSH(&state);

	return R_TOS;
}


/***********************************************************************
**
*/	REBNATIVE(unless)
/*
***********************************************************************/
{
	if (IS_CONDITIONAL_TRUE(D_ARG(1))) return R_NONE;
	if (IS_BLOCK(D_ARG(2)) && !D_REF(3) /* not using /ONLY */) {
		DO_BLK(D_ARG(2));
		return R_TOS1;
	}
	return R_ARG2;
}
