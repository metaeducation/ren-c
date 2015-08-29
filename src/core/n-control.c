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
*/	static void Protect_Word(REBVAL *value, REBCNT flags)
/*
***********************************************************************/
{
	if (GET_FLAG(flags, PROT_WORD)) {
		if (GET_FLAG(flags, PROT_SET)) VAL_SET_EXT(value, EXT_WORD_LOCK);
		else VAL_CLR_EXT(value, EXT_WORD_LOCK);
	}

	if (GET_FLAG(flags, PROT_HIDE)) {
		if GET_FLAG(flags, PROT_SET) VAL_SET_EXT(value, EXT_WORD_HIDE);
		else VAL_CLR_EXT(value, EXT_WORD_HIDE);
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
*/	static int Protect(struct Reb_Call *call_, REBCNT flags)
/*
**	Common arguments between protect and unprotect:
**
**		1: value
**		2: /deep  - recursive
**		3: /words  - list of words
**		4: /values - list of values
**
**	Protect takes a HIDE parameter as #5.
**
***********************************************************************/
{
	REBVAL *val = D_ARG(1);

	// flags has PROT_SET bit (set or not)

	Check_Security(SYM_PROTECT, POL_WRITE, val);

	if (D_REF(2)) SET_FLAG(flags, PROT_DEEP);
	//if (D_REF(3)) SET_FLAG(flags, PROT_WORD);

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
			REBVAL safe;
			for (val = VAL_BLK_DATA(val); NOT_END(val); val++) {
				if (IS_WORD(val)) {
					// !!! Temporary and ugly cast; since we *are* PROTECT
					// we allow ourselves to get mutable references to even
					// protected values so we can no-op protect them.
					val2 = m_cast(REBVAL*, GET_VAR(val));
				}
				else if (IS_PATH(val)) {
					const REBVAL *path = val;
					if (Do_Path(&safe, &path, 0)) {
						val2 = val; // !!! comment said "found a function"
					} else {
						val2 = &safe;
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

	if (GET_FLAG(flags, PROT_HIDE)) raise Error_0(RE_BAD_REFINES);

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

	// Default result for 'all []'
	SET_TRUE(D_OUT);

	while (index < SERIES_TAIL(block)) {
		index = Do_Next_May_Throw(D_OUT, block, index);
		if (index == THROWN_FLAG) break;
		// !!! UNSET! should be an error, CC#564 (Is there a better error?)
		/* if (IS_UNSET(D_OUT)) raise Error_0(RE_NO_RETURN); */
		if (IS_CONDITIONAL_FALSE(D_OUT)) {
			SET_TRASH_SAFE(D_OUT);
			return R_NONE;
		}
	}
	return R_OUT;
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
		index = Do_Next_May_Throw(D_OUT, block, index);
		if (index == THROWN_FLAG) return R_OUT;

		// !!! UNSET! should be an error, CC#564 (Is there a better error?)
		/* if (IS_UNSET(D_OUT)) raise Error_0(RE_NO_RETURN); */

		if (!IS_CONDITIONAL_FALSE(D_OUT) && !IS_UNSET(D_OUT)) return R_OUT;
	}

	return R_NONE;
}


/***********************************************************************
**
*/	REBNATIVE(apply)
/*
**		1: func
**		2: block
**		3: /only
**
***********************************************************************/
{
	REBVAL * func = D_ARG(1);
	REBVAL * block = D_ARG(2);
	REBOOL reduce = !D_REF(3);

	Apply_Block(
		D_OUT, func, VAL_SERIES(block), VAL_INDEX(block), reduce
	);
	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(attempt)
/*
***********************************************************************/
{
	REBOL_STATE state;
	const REBVAL *error;

	PUSH_TRAP(&error, &state);

// The first time through the following code 'error' will be NULL, but...
// `raise Error` can longjmp here, so 'error' won't be NULL *if* that happens!

	if (error) return R_NONE;

	if (Do_Block_Throws(D_OUT, VAL_SERIES(D_ARG(1)), VAL_INDEX(D_ARG(1)))) {
		// This means that D_OUT is a THROWN() value, but we have
		// no special processing to apply.  Fall through and return it.
	}

	DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);

	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(break)
/*
**		1: /with
**		2: value
**		3: /return (deprecated)
**		4: return-value
**
**	While BREAK is implemented via a THROWN() value that bubbles up
**	through the stack, it may not ultimately use the WORD! of BREAK
**	as its /NAME.
**
***********************************************************************/
{
	REBVAL *value = D_REF(1) ? D_ARG(2) : (D_REF(3) ? D_ARG(4) : UNSET_VALUE);

	Val_Init_Word_Unbound(D_OUT, REB_WORD, SYM_BREAK);

	CONVERT_NAME_TO_THROWN(D_OUT, value);

	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(case)
/*
**	1: block
**	2: /all
**	3: /only
**
***********************************************************************/
{
	// We leave D_ARG(1) alone, it is holding 'block' alive from GC
	REBSER *block = VAL_SERIES(D_ARG(1));
	REBCNT index = VAL_INDEX(D_ARG(1));

	// Save refinements to booleans to free up their call frame slots
	REBFLG all = D_REF(2);
	REBFLG only = D_REF(3);

	// reuse refinement slots for GC safety (pointers are optimized out)
	REBVAL * const condition_result = D_ARG(2);
	REBVAL * const body_result = D_ARG(3);

	// CASE is in the same family as IF/UNLESS/EITHER, so if there is no
	// matching condition it will return a NONE!.  Set that as default.

	SET_NONE(D_OUT);

	while (index < SERIES_TAIL(block)) {

		index = Do_Next_May_Throw(condition_result, block, index);

		if (index == THROWN_FLAG) {
			*D_OUT = *condition_result; // is a RETURN, BREAK, THROW...
			return R_OUT;
		}

		if (index == END_FLAG) raise Error_0(RE_PAST_END);

		if (IS_UNSET(condition_result)) raise Error_0(RE_NO_RETURN);

		// We DO the next expression, rather than just assume it is a
		// literal block.  That allows you to write things like:
		//
		//     condition: true
		//     case [condition 10 + 20] ;-- returns 30
		//
		// But we need to DO regardless of the condition being true or
		// false.  Rebol2 would just skip over one item (the 10 in this
		// case) and get an error.  Code not in blocks must be evaluated
		// even if false, as it is with 'if false (print "eval'd")'
		//
		// If the source was a literal block then the Do_Next_May_Throw
		// will *probably* be a no-op, but consider infix operators:
		//
		//     case [true [stuff] + [more stuff]]
		//
		// Until such time as DO guarantees such things aren't legal,
		// CASE must evaluate block literals too.

		if (
			LEGACY(OPTIONS_BROKEN_CASE_SEMANTICS)
			&& IS_CONDITIONAL_FALSE(condition_result)
		) {
			// case [true add 1 2] => 3
			// case [false add 1 2] => 2 ;-- in Rebol2
			index++;

			// forgets the last evaluative result for a TRUE condition
			// when /ALL is set (instead of keeping it to return)
			SET_NONE(D_OUT);
			continue;
		}

		index = Do_Next_May_Throw(body_result, block, index);

		if (index == THROWN_FLAG) {
			*D_OUT = *body_result; // is a RETURN, BREAK, THROW...
			return R_OUT;
		}

		if (index == END_FLAG) {
			if (LEGACY(OPTIONS_BROKEN_CASE_SEMANTICS)) {
				// case [first [a b c]] => true ;-- in Rebol2
				return R_TRUE;
			}

			// case [first [a b c]] => **error**
			raise Error_0(RE_PAST_END);
		}

		if (IS_CONDITIONAL_TRUE(condition_result)) {

			if (!only && IS_BLOCK(body_result)) {
				// If we're not using the /ONLY switch and it's a block,
				// we'll need two evaluations for things like:
				//
				//     stuff: [print "This will be printed"]
				//     case [true stuff]
				//
				if (Do_Block_Throws(
					D_OUT, VAL_SERIES(body_result), VAL_INDEX(body_result)
				)) {
					return R_OUT;
				}
			}
			else {
				// With /ONLY (or a non-block) don't do more evaluation, so
				// for the above that's: [print "This will be printed"]

				*D_OUT = *body_result;
			}

			if (LEGACY(OPTIONS_BROKEN_CASE_SEMANTICS)) {
				if (IS_UNSET(D_OUT)) {
					// case [true [] false [1 + 2]] => true ;-- in Rebol2
					SET_TRUE(D_OUT);
				}
			}

			// One match is enough to return the result now, unless /ALL
			if (!all) return R_OUT;
		}
	}

	// Returns the evaluative result of the last body whose condition was
	// conditionally true, or defaults to NONE if there weren't any

	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(catch)
/*
**	1 block
**	2 /name
**	3 name-list
**	4 /quit
**	5 /any
**	6 /with
**	7 handler
**
**	There's a refinement for catching quits, and CATCH/ANY will not
**	alone catch it (you have to CATCH/ANY/QUIT).  The use of the
**	WORD! QUIT is pending review, and when full label values are
**	available it will likely be changed to at least get the native
**	(e.g. equal to THROW with /NAME :QUIT instead of /NAME 'QUIT)
**
***********************************************************************/
{
	REBVAL * const block = D_ARG(1);

	const REBOOL named = D_REF(2);
	REBVAL * const name_list = D_ARG(3);

	// We save the values into booleans (and reuse their GC-protected slots)
	const REBOOL quit = D_REF(4);
	const REBOOL any = D_REF(5);

	const REBOOL with = D_REF(6);
	REBVAL * const handler = D_ARG(7);

	// /ANY would override /NAME, so point out the potential confusion
	if (any && named) raise Error_0(RE_BAD_REFINES);

	if (Do_Block_Throws(D_OUT, VAL_SERIES(block), VAL_INDEX(block))) {
		if (
			(any && (!IS_WORD(D_OUT) || VAL_WORD_SYM(D_OUT) != SYM_QUIT))
			|| (quit && IS_WORD(D_OUT) && VAL_WORD_SYM(D_OUT) == SYM_QUIT)
		) {
			goto was_caught;
		}

		if (named) {
			// We use equal? by way of Compare_Modify_Values, and re-use the
			// refinement slots for the mutable space
			REBVAL * const temp1 = D_ARG(4);
			REBVAL * const temp2 = D_ARG(5);

			// !!! The reason we're copying isn't so the OPT_VALUE_THROWN bit
			// won't confuse the equality comparison...but would it have?

			if (IS_BLOCK(name_list)) {
				// Test all the words in the block for a match to catch
				REBVAL *candidate = VAL_BLK_DATA(name_list);
				for (; NOT_END(candidate); candidate++) {
					// !!! Should we test a typeset for illegal name types?
					if (IS_BLOCK(candidate))
						raise Error_1(RE_INVALID_ARG, name_list);

					*temp1 = *candidate;
					*temp2 = *D_OUT;

					// Return the THROW/NAME's arg if the names match
					// !!! 0 means equal?, but strict-equal? might be better
					if (Compare_Modify_Values(temp1, temp2, 0))
						goto was_caught;
				}
			}
			else {
				*temp1 = *name_list;
				*temp2 = *D_OUT;

				// Return the THROW/NAME's arg if the names match
				// !!! 0 means equal?, but strict-equal? might be better
				if (Compare_Modify_Values(temp1, temp2, 0))
					goto was_caught;
			}
		}
		else {
			// Return THROW's arg only if it did not have a /NAME supplied
			if (IS_NONE(D_OUT))
				goto was_caught;
		}
	}

	return R_OUT;

was_caught:
	if (with) {
		if (IS_BLOCK(handler)) {
			// There's no way to pass args to a block (so just DO it)
			if (Do_Block_Throws(
				D_OUT, VAL_SERIES(handler), VAL_INDEX(handler)
			)) {
				return R_OUT;
			}
			return R_OUT;
		}
		else if (ANY_FUNC(handler)) {
			// We again re-use the refinement slots, but this time as mutable
			// space protected from GC for the handler's arguments
			REBVAL *thrown_arg = D_ARG(4);
			REBVAL *thrown_name = D_ARG(5);

			TAKE_THROWN_ARG(thrown_arg, D_OUT);
			*thrown_name = *D_OUT; // THROWN bit cleared by TAKE_THROWN_ARG

			// We will accept a function of arity 0, 1, or 2 as a CATCH/WITH
			// handler.  If it is arity 1 it will get just the thrown value,
			// If it is arity 2 it will get the value and the throw name.

			REBVAL *param = BLK_SKIP(VAL_FUNC_WORDS(handler), 1);
			if (NOT_END(param) && !TYPE_CHECK(param, VAL_TYPE(thrown_arg))) {
				raise Error_3(
					RE_EXPECT_ARG,
					Type_Of(handler),
					param,
					Type_Of(thrown_arg)
				);
			}

			if (NOT_END(param)) ++param;

			if (NOT_END(param) && !TYPE_CHECK(param, VAL_TYPE(thrown_name))) {
				raise Error_3(
					RE_EXPECT_ARG,
					Type_Of(handler),
					param,
					Type_Of(thrown_name)
				);
			}

			if (NOT_END(param)) param++;

			if (NOT_END(param) && !IS_REFINEMENT(param)) {
				// We go lower in arity, but don't make up arg values
				raise Error_1(RE_NEED_VALUE, param);
			}

			// !!! As written, Apply_Func will ignore extra arguments.
			// This means we can pass a lower arity function.  The
			// effect is desirable, though having Apply_Func be cavalier
			// about extra arguments may not be the best way to do it.

			Apply_Func(D_OUT, handler, thrown_arg, thrown_name, NULL);
			return R_OUT;
		}
	}

	// If no handler, just return the caught thing
	TAKE_THROWN_ARG(D_OUT, D_OUT);
	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(throw)
/*
***********************************************************************/
{
	REBVAL * const value = D_ARG(1);
	REBOOL named = D_REF(2);
	REBVAL * const name_value = D_ARG(3);

	if (named) {
		// blocks as names would conflict with name_list feature in catch
		assert(!IS_BLOCK(name_value));
		*D_OUT = *name_value;
	}
	else {
		// None values serving as representative of THROWN() means "no name"

		// !!! This convention might be a bit "hidden" while debugging if
		// one misses the THROWN() bit.  But that's true of THROWN() values
		// in general.  Debug output should make noise about THROWNs
		// whenever it sees them.

		SET_NONE(D_OUT);
	}

	CONVERT_NAME_TO_THROWN(D_OUT, value);

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
**		1: value "Block to compose"
**		2: /deep "Compose nested blocks"
**		3: /only "Inserts a block value as a block"
**		4: /into "Output results into a block with no intermediate storage"
**		5: target
**
**		!!! Should 'compose quote (a (1 + 2) b)' give back '(a 3 b)' ?
**		!!! What about 'compose quote a/(1 + 2)/b' ?
**
***********************************************************************/
{
	REBVAL *value = D_ARG(1);
	REBOOL into = D_REF(4);

	// Only composes BLOCK!, all other arguments evaluate to themselves
	if (!IS_BLOCK(value)) return R_ARG1;

	// Compose expects out to contain the target if /INTO
	if (into) *D_OUT = *D_ARG(5);

	Compose_Block(D_OUT, value, D_REF(2), D_REF(3), into);

	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(continue)
/*
**	While CONTINUE is implemented via a THROWN() value that bubbles up
**	through the stack, it may not ultimately use the WORD! of CONTINUE
**	as its /NAME.
**
***********************************************************************/
{
	Val_Init_Word_Unbound(D_OUT, REB_WORD, SYM_CONTINUE);
	CONVERT_NAME_TO_THROWN(D_OUT, UNSET_VALUE);

	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(do)
/*
***********************************************************************/
{
	REBVAL * const value = D_ARG(1);
	REBVAL * const args_ref = D_ARG(2);
	REBVAL * const arg = D_ARG(3);
	REBVAL * const next_ref = D_ARG(4);
	REBVAL * const var = D_ARG(5);

	REBVAL out;

	switch (VAL_TYPE(value)) {

	case REB_BLOCK:
	case REB_PAREN:
		if (D_REF(4)) { // next
			VAL_INDEX(value) = Do_Next_May_Throw(
				D_OUT, VAL_SERIES(value), VAL_INDEX(value)
			);

			if (VAL_INDEX(value) == THROWN_FLAG) {
				// We're going to return the value in D_OUT anyway, but
				// if we looked at D_OUT we'd have to check this first
			}

			if (VAL_INDEX(value) == END_FLAG) {
				VAL_INDEX(value) = VAL_TAIL(value);
				Set_Var(D_ARG(5), value);
				SET_TRASH_SAFE(D_OUT);
				return R_UNSET;
			}
			Set_Var(D_ARG(5), value); // "continuation" of block
			return R_OUT;
		}

		if (Do_Block_Throws(D_OUT, VAL_SERIES(value), 0))
			return R_OUT;

		return R_OUT;

    case REB_NATIVE:
	case REB_ACTION:
    case REB_COMMAND:
    case REB_REBCODE:
    case REB_CLOSURE:
	case REB_FUNCTION:
		VAL_SET_EXT(value, EXT_FUNC_REDO);
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
		raise Error_Is(value);

	case REB_BINARY:
	case REB_STRING:
	case REB_URL:
	case REB_FILE:
		// DO native and system/intrinsic/do* must use same arg list:
		if (!Do_Sys_Func(
			D_OUT,
			SYS_CTX_DO_P,
			value,
			args_ref,
			arg,
			next_ref,
			var,
			NULL
		)) {
			// Was THROW, RETURN, EXIT, QUIT etc...
			// No special handling, just return as we were going to
		}
		return R_OUT;

	case REB_TASK:
		Do_Task(value);
		return R_ARG1;

	case REB_SET_WORD:
	case REB_SET_PATH:
		raise Error_Invalid_Arg(value);

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
		if (Do_Block_Throws(D_OUT, VAL_SERIES(D_ARG(argnum)), 0))
			return R_OUT;

		return R_OUT;
	}

	return argnum == 2 ? R_ARG2 : R_ARG3;
}


/***********************************************************************
**
*/	REBNATIVE(exit)
/*
**	1: /with
**	2: value
**
**	While EXIT is implemented via a THROWN() value that bubbles up
**	through the stack, it may not ultimately use the WORD! of EXIT
**	as its /NAME.
**
***********************************************************************/
{
	if (LEGACY(OPTIONS_EXIT_FUNCTIONS_ONLY))
		Val_Init_Word_Unbound(D_OUT, REB_WORD, SYM_RETURN);
	else
		Val_Init_Word_Unbound(D_OUT, REB_WORD, SYM_EXIT);

	CONVERT_NAME_TO_THROWN(D_OUT, D_REF(1) ? D_ARG(2) : UNSET_VALUE);

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
		if (Do_Block_Throws(D_OUT, VAL_SERIES(D_ARG(2)), 0))
			return R_OUT;

		return R_OUT;
	}
	return R_ARG2;
}


/***********************************************************************
**
*/	REBNATIVE(protect)
/*
***********************************************************************/
{
	REBCNT flags = FLAGIT(PROT_SET);

	if (D_REF(5)) SET_FLAG(flags, PROT_HIDE);
	else SET_FLAG(flags, PROT_WORD); // there is no unhide

	// accesses arguments 1 - 4
	return Protect(call_, flags);
}


/***********************************************************************
**
*/	REBNATIVE(unprotect)
/*
***********************************************************************/
{
	// accesses arguments 1 - 4
	return Protect(call_, FLAGIT(PROT_WORD));
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
		REBOOL into = D_REF(5);

		if (into)
			*D_OUT = *D_ARG(6);

		if (D_REF(2))
			Reduce_Block_No_Set(D_OUT, ser, index, into);
		else if (D_REF(3))
			Reduce_Only(D_OUT, ser, index, D_ARG(4), into);
		else
			Reduce_Block(D_OUT, ser, index, into);

		return R_OUT;
	}

	return R_ARG1;
}


/***********************************************************************
**
*/	REBNATIVE(return)
/*
**	The implementation of RETURN here is a simple THROWN() value and
**	has no "definitional scoping"--a temporary state of affairs.
**
***********************************************************************/
{
	REBVAL *arg = D_ARG(1);

	Val_Init_Word_Unbound(D_OUT, REB_WORD, SYM_RETURN);
	CONVERT_NAME_TO_THROWN(D_OUT, arg);

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
	REBVAL *case_val = VAL_BLK_DATA(D_ARG(2));
	REBOOL all = D_REF(5);
	REBOOL found = FALSE;

	for (; NOT_END(case_val); case_val++) {

		// Look for the next *non* block case value to try to match
		if (!IS_BLOCK(case_val) && 0 == Cmp_Value(D_ARG(1), case_val, FALSE)) {

			// Skip ahead to try and find a block, to treat as code
			while (!IS_BLOCK(case_val) && NOT_END(case_val)) case_val++;
			if (IS_END(case_val)) break;

			found = TRUE;

			// Evaluate code block, but if result is THROWN() then return it
			if (Do_Block_Throws(D_OUT, VAL_SERIES(case_val), 0)) return R_OUT;

			if (!all) return R_OUT;
		}
	}

	if (!found && IS_BLOCK(D_ARG(4))) {
		if (Do_Block_Throws(D_OUT, VAL_SERIES(D_ARG(4)), 0))
			return R_OUT;

		return R_OUT;
	}

	return R_NONE;
}


/***********************************************************************
**
*/	REBNATIVE(trap)
/*
**		1: block
**		2: /with
**		3: handler
**
***********************************************************************/
{
	REBVAL * const block = D_ARG(1);
	const REBFLG with = D_REF(2);
	REBVAL * const handler = D_ARG(3);

	REBOL_STATE state;
	const REBVAL *error;

	PUSH_TRAP(&error, &state);

// The first time through the following code 'error' will be NULL, but...
// `raise Error` can longjmp here, so 'error' won't be NULL *if* that happens!

	if (error) {
		if (with) {
			if (IS_BLOCK(handler)) {
				// There's no way to pass 'error' to a block (so just DO it)
				if (Do_Block_Throws(
					D_OUT, VAL_SERIES(handler), VAL_INDEX(handler)
				)) {
					return R_OUT;
				}
				return R_OUT;
			}
			else if (ANY_FUNC(handler)) {
				REBVAL *param = BLK_SKIP(VAL_FUNC_WORDS(handler), 1);

				// We will accept a function of arity 0 or 1 as a TRAP/WITH
				// handler.  If it is arity 1 it will get the error.

				if (NOT_END(param) && !TYPE_CHECK(param, VAL_TYPE(error))) {
					// If handler takes an arg, it must take ERROR!
					raise Error_1(RE_TRAP_WITH_EXPECTS, param);
				}

				if (NOT_END(param)) param++;

				if (NOT_END(param) && !IS_REFINEMENT(param)) {
					// We go lower in arity, but don't make up arg values
					raise Error_1(RE_NEED_VALUE, param);
				}

				// !!! As written, Apply_Func will ignore extra arguments.
				// This means we can pass a lower arity function.  The
				// effect is desirable, though having Apply_Func be cavalier
				// about extra arguments may not be the best way to do it.
				Apply_Func(D_OUT, handler, error, NULL);
				return R_OUT;
			}

			panic Error_0(RE_MISC); // should not be possible (type-checking)
		}

		*D_OUT = *error;
		return R_OUT;
	}

	if (Do_Block_Throws(D_OUT, VAL_SERIES(block), VAL_INDEX(block))) {
		// Note that we are interested in when errors are raised, which
		// causes a tricky C longjmp() to the code above.  Yet a THROW
		// is different from that, and offers an opportunity to each
		// DO'ing stack level along the way to CATCH the thrown value
		// (with no need for something like the PUSH_TRAP above).
		//
		// We're being given that opportunity here, but doing nothing
		// and just returning the THROWN thing for other stack levels
		// to look at.  For the construct which does let you catch a
		// throw, see REBNATIVE(catch), which has code for this case.
	}

	DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);

	return R_OUT;
}


/***********************************************************************
**
*/	REBNATIVE(unless)
/*
***********************************************************************/
{
	if (IS_CONDITIONAL_TRUE(D_ARG(1))) return R_NONE;

	if (IS_BLOCK(D_ARG(2)) && !D_REF(3) /* not using /ONLY */) {
		if (Do_Block_Throws(D_OUT, VAL_SERIES(D_ARG(2)), 0))
			return R_OUT;

		return R_OUT;
	}

	return R_ARG2;
}
