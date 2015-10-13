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
**  Module:  c-function.c
**  Summary: support for functions, actions, closures and routines
**  Section: core
**  Author:  Carl Sassenrath, Shixin Zeng
**  Notes:
**
***********************************************************************/
/*
	Structure of functions:

		spec - interface spec block
		body - body code
		args - args list (see below)

	Args list is a block of word+values:

		word - word, 'word, :word, /word
		value - typeset! or none (valid datatypes)

	Args list provides:

		1. specifies arg order, arg kind (e.g. 'word)
		2. specifies valid datatypes (typesets)
		3. used for word and type in error output
		4. used for debugging tools (stack dumps)
		5. not used for MOLD (spec is used)
		6. used as a (pseudo) frame of function variables

*/

#include "sys-core.h"

/***********************************************************************
**
*/	REBSER *List_Func_Words(const REBVAL *func)
/*
**		Return a block of function words, unbound.
**		Note: skips 0th entry.
**
***********************************************************************/
{
	REBSER *series = VAL_FUNC_PARAMLIST(func);
	REBVAL *typeset = BLK_SKIP(series, 1);

	REBSER *block = Make_Array(SERIES_TAIL(series));

	REBCNT n;
	for (n = 1; n < SERIES_TAIL(series); typeset++, n++) {
		enum Reb_Kind kind;
		if (VAL_GET_EXT(typeset, EXT_TYPESET_REFINEMENT))
			kind = REB_REFINEMENT;
		else if (VAL_GET_EXT(typeset, EXT_TYPESET_QUOTE)) {
			if (VAL_GET_EXT(typeset, EXT_TYPESET_EVALUATE))
				kind = REB_LIT_WORD;
			else
				kind = REB_GET_WORD;
		}
		else {
			// Currently there's no meaning for non-quoted non-evaluating
			// things (only 3 param types for foo:, 'foo, :foo)
			assert(VAL_GET_EXT(typeset, EXT_TYPESET_EVALUATE));
			kind = REB_WORD;
		}

		Val_Init_Word_Unbound(
			Alloc_Tail_Array(block), kind, VAL_TYPESET_SYM(typeset)
		);
	}

	return block;
}


/***********************************************************************
**
*/	REBSER *List_Func_Typesets(REBVAL *func)
/*
**		Return a block of function arg typesets.
**		Note: skips 0th entry.
**
***********************************************************************/
{
	REBSER *series = VAL_FUNC_PARAMLIST(func);
	REBVAL *typeset = BLK_SKIP(series, 1);

	REBSER *block = Make_Array(SERIES_TAIL(series));

	REBCNT n;
	for (n = 1; n < SERIES_TAIL(series); typeset++, n++) {
		REBVAL *value = Alloc_Tail_Array(block);
		*value = *typeset;

		// !!! It's already a typeset, but this will clear out the header
		// bits.  This may not be desirable over the long run (what if
		// a typeset wishes to encode hiddenness, protectedness, etc?)

		VAL_SET(value, REB_TYPESET);
	}

	return block;
}


/***********************************************************************
**
*/	REBSER *Check_Func_Spec(REBSER *spec, REBYTE *exts)
/*
**		Check function spec of the form:
**
**		["description" arg "notes" [type! type2! ...] /ref ...]
**
**		Throw an error for invalid values.
**
***********************************************************************/
{
	REBVAL *item;
	REBSER *keylist;
	REBVAL *typeset;

	*exts = 0;

	keylist = Collect_Frame(
		NULL, BLK_HEAD(spec), BIND_ALL | BIND_NO_DUP | BIND_NO_SELF
	);

	// First position is "self", but not used...
	typeset = BLK_HEAD(keylist);

	// !!! needs more checks
	for (item = BLK_HEAD(spec); NOT_END(item); item++) {
		switch (VAL_TYPE(item)) {
		case REB_BLOCK:
			if (typeset == BLK_HEAD(keylist)) {
				// !!! Rebol2 had the ability to put a block in the first
				// slot before any parameters, in which you could put words.
				// This is deprecated in favor of the use of tags.  We permit
				// [catch] and [throw] during Rebol2 => Rebol3 migration.

				REBVAL *attribute = VAL_BLK_DATA(item);
				for (; NOT_END(attribute); attribute++) {
					if (IS_WORD(attribute)) {
						if (VAL_WORD_SYM(attribute) == SYM_CATCH)
							continue; // ignore it;
						if (VAL_WORD_SYM(attribute) == SYM_THROW) {
							// Basically a synonym for <transparent>
							SET_FLAG(*exts, EXT_FUNC_TRANSPARENT);
							continue;
						}
						// no other words supported, fall through to error
					}
					raise Error_1(RE_BAD_FUNC_DEF, item);
				}
				break; // leading block handled if we get here, no more to do
			}

			// Turn block into typeset for parameter at current index
			// Note: Make_Typeset leaves VAL_TYPESET_SYM as-is
			Make_Typeset(VAL_BLK_HEAD(item), typeset, 0);
			break;

		case REB_STRING:
			// !!! Documentation strings are ignored, but should there be
			// some canon form be enforced?  Right now you can write many
			// forms that may not be desirable to have in the wild:
			//
			//		func [foo [type!] {doc string :-)}]
			//		func [foo {doc string :-/} [type!]]
			//		func [foo {doc string1 :-/} {doc string2 :-(} [type!]]
			//
			// It's currently HELP that has to sort out the variant forms
			// but there's nothing stopping them.
			break;

		case REB_INTEGER:
			// special case used by datatype testing actions, e.g. STRING?
			break;

		case REB_WORD:
			typeset++;
			assert(
				IS_TYPESET(typeset)
				&& VAL_TYPESET_SYM(typeset) == VAL_WORD_SYM(item)
			);
			VAL_SET_EXT(typeset, EXT_TYPESET_EVALUATE);
			break;

		case REB_GET_WORD:
			typeset++;
			assert(
				IS_TYPESET(typeset)
				&& VAL_TYPESET_SYM(typeset) == VAL_WORD_SYM(item)
			);
			VAL_SET_EXT(typeset, EXT_TYPESET_QUOTE);
			break;

		case REB_LIT_WORD:
			typeset++;
			assert(
				IS_TYPESET(typeset)
				&& VAL_TYPESET_SYM(typeset) == VAL_WORD_SYM(item)
			);
			VAL_SET_EXT(typeset, EXT_TYPESET_QUOTE);
			// will actually only evaluate get-word!, get-path!, and paren!
			VAL_SET_EXT(typeset, EXT_TYPESET_EVALUATE);
			break;

		case REB_REFINEMENT:
			typeset++;
			assert(
				IS_TYPESET(typeset)
				&& VAL_TYPESET_SYM(typeset) == VAL_WORD_SYM(item)
			);
			VAL_SET_EXT(typeset, EXT_TYPESET_REFINEMENT);

		#if !defined(NDEBUG)
			// Because Mezzanine functions are written to depend on the idea
			// that when they get a refinement it will be a WORD! and not a
			// LOGIC!, we have to capture the desire to get LOGIC! vs WORD!
			// at function creation time...not dispatch time.  We encode the
			// bit in the refinement's typeset that it accepts.
			if (LEGACY(OPTIONS_REFINEMENTS_TRUE)) {
				VAL_TYPESET_BITS(typeset) =
					(FLAGIT_64(REB_LOGIC) | FLAGIT_64(REB_NONE));
				break;
			}
		#endif
			// Refinements can nominally be only WORD! or NONE!
			VAL_TYPESET_BITS(typeset) =
				(FLAGIT_64(REB_WORD) | FLAGIT_64(REB_NONE));
			break;

		case REB_TAG:
			// Tags are used to specify some EXT_FUNC opts switches.  At
			// present they are only allowed at the head of the spec block,
			// to try and keep things in at least a slightly canon format.
			// This may or may not be relaxed in the future.
			if (typeset != BLK_HEAD(keylist))
				raise Error_1(RE_BAD_FUNC_DEF, item);

			if (0 == Compare_String_Vals(item, ROOT_INFIX_TAG, TRUE))
				SET_FLAG(*exts, EXT_FUNC_INFIX);
			else if (0 == Compare_String_Vals(item, ROOT_TRANSPARENT_TAG, TRUE))
				SET_FLAG(*exts, EXT_FUNC_TRANSPARENT);
			else
				raise Error_1(RE_BAD_FUNC_DEF, item);
			break;

		case REB_SET_WORD:
		default:
			raise Error_1(RE_BAD_FUNC_DEF, item);
		}
	}

	MANAGE_SERIES(keylist);
	return keylist;
}


/***********************************************************************
**
*/	void Make_Native(REBVAL *value, REBSER *spec, REBFUN func, REBINT type)
/*
***********************************************************************/
{
	REBYTE exts;
	//Print("Make_Native: %s spec %d", Get_Sym_Name(type+1), SERIES_TAIL(spec));
	ENSURE_SERIES_MANAGED(spec);
	VAL_FUNC_SPEC(value) = spec;
	VAL_FUNC_PARAMLIST(value) = Check_Func_Spec(spec, &exts);

	// We don't expect special flags on natives like <transparent>, <infix>
	assert(exts == 0);

	VAL_FUNC_CODE(value) = func;
	VAL_SET(value, type);
}


/***********************************************************************
**
*/	REBFLG Make_Function(REBVAL *out, REBCNT type, REBVAL *def)
/*
***********************************************************************/
{
	REBVAL *spec;
	REBVAL *body;
	REBCNT len;
	REBYTE exts;

	if (
		!IS_BLOCK(def)
		|| (len = VAL_LEN(def)) < 2
		|| !IS_BLOCK(spec = VAL_BLK_HEAD(def))
	) return FALSE;

	body = VAL_BLK_SKIP(def, 1);

	VAL_FUNC_SPEC(out) = VAL_SERIES(spec);
	VAL_FUNC_PARAMLIST(out) = Check_Func_Spec(VAL_SERIES(spec), &exts);

	if (type != REB_COMMAND) {
		if (len != 2 || !IS_BLOCK(body)) return FALSE;
		VAL_FUNC_BODY(out) = VAL_SERIES(body);
	}
	else
		Make_Command(out, def);

	VAL_SET(out, type); // clears exts and opts in header...
	VAL_EXTS_DATA(out) = exts; // ...so we set this after that point

	if (type == REB_FUNCTION || type == REB_CLOSURE)
		Bind_Relative(
			VAL_FUNC_PARAMLIST(out), VAL_FUNC_PARAMLIST(out), VAL_FUNC_BODY(out)
		);

	return TRUE;
}


/***********************************************************************
**
*/	void Copy_Function(REBVAL *out, const REBVAL *src)
/*
***********************************************************************/
{
	if (IS_FUNCTION(src) || IS_CLOSURE(src)) {
		// !!! A closure's "archetype" never operates on its body directly,
		// and there is currently no way to get a reference to a closure
		// "instance" (an ANY-FUNCTION value with the copied body in it).
		// Making a copy of the body here is likely superfluous right now.

		// Need to pick up the infix flag and any other settings.
		out->flags = src->flags;

		// We can reuse the spec series.  A more nuanced form of function
		// copying might let you change the spec as part of the process and
		// keep the body (or vice versa), but would need to check to make
		// sure they were compatible with the substitution.
		VAL_FUNC_SPEC(out) = VAL_SERIES(src);

		// Copy the identifying word series, so that the function has a
		// unique identity on the stack from the one it is copying.
		VAL_FUNC_PARAMLIST(out) = Copy_Array_Shallow(VAL_FUNC_PARAMLIST(src));
		MANAGE_SERIES(VAL_FUNC_PARAMLIST(out));

		// Copy the body and rebind its word references to the locals.
		VAL_FUNC_BODY(out) = Copy_Array_Deep_Managed(VAL_FUNC_BODY(src));
		Bind_Relative(
			VAL_FUNC_PARAMLIST(out), VAL_FUNC_PARAMLIST(out), VAL_FUNC_BODY(out)
		);
	}
	else {
		// Natives, actions, etc. do not have bodies that can accumulate
		// state, and hence the only meaning of "copying" a function is just
		// copying its value bits verbatim.
		*out = *src;
	}
}


/***********************************************************************
**
*/	void Do_Native(const REBVAL *func)
/*
***********************************************************************/
{
	REBVAL *out = DSF_OUT(DSF);
	REB_R ret;

	Eval_Natives++;

	ret = VAL_FUNC_CODE(func)(DSF);

	switch (ret) {
	case R_OUT: // for compiler opt
		break;
	case R_NONE:
		SET_NONE(out);
		break;
	case R_UNSET:
		SET_UNSET(out);
		break;
	case R_TRUE:
		SET_TRUE(out);
		break;
	case R_FALSE:
		SET_FALSE(out);
		break;
	case R_ARG1:
		*out = *DSF_ARG(DSF, 1);
		break;
	case R_ARG2:
		*out = *DSF_ARG(DSF, 2);
		break;
	case R_ARG3:
		*out = *DSF_ARG(DSF, 3);
		break;
	default:
		assert(FALSE);
	}
}


/***********************************************************************
**
*/	void Do_Action(const REBVAL *func)
/*
***********************************************************************/
{
	REBVAL *out = DSF_OUT(DSF);
	REBCNT type = VAL_TYPE(DSF_ARG(DSF, 1));
	REBACT action;
	REB_R ret;

	Eval_Natives++;

	assert(type < REB_MAX);

	// Handle special datatype test cases (eg. integer?)
	if (VAL_FUNC_ACT(func) == 0) {
		VAL_SET(out, REB_LOGIC);
		VAL_LOGIC(out) = (type == VAL_INT64(BLK_LAST(VAL_FUNC_SPEC(func))));
		return;
	}

	action = Value_Dispatch[type];
	if (!action) raise Error_Illegal_Action(type, VAL_FUNC_ACT(func));
	ret = action(DSF, VAL_FUNC_ACT(func));

	switch (ret) {
	case R_OUT: // for compiler opt
		break;
	case R_NONE:
		SET_NONE(out);
		break;
	case R_UNSET:
		SET_UNSET(out);
		break;
	case R_TRUE:
		SET_TRUE(out);
		break;
	case R_FALSE:
		SET_FALSE(out);
		break;
	case R_ARG1:
		*out = *DSF_ARG(DSF, 1);
		break;
	case R_ARG2:
		*out = *DSF_ARG(DSF, 2);
		break;
	case R_ARG3:
		*out = *DSF_ARG(DSF, 3);
		break;
	default:
		assert(FALSE);
	}
}


/***********************************************************************
**
*/	void Do_Function(const REBVAL *func)
/*
***********************************************************************/
{
	REBVAL *out = DSF_OUT(DSF);

	Eval_Functions++;

	if (Do_Block_Throws(out, VAL_FUNC_BODY(func), 0)) {
		if (
			IS_WORD(out) &&
			(VAL_WORD_SYM(out) == SYM_RETURN || VAL_WORD_SYM(out) == SYM_EXIT)
		) {
			if (!VAL_GET_EXT(func, EXT_FUNC_TRANSPARENT))
				TAKE_THROWN_ARG(out, out);
		}
	}
}


/***********************************************************************
**
*/	void Do_Closure(const REBVAL *func)
/*
**		Do a closure by cloning its body and rebinding it to
**		a new frame of words/values.
**
***********************************************************************/
{
	REBSER *body;
	REBSER *frame;
	REBVAL *out = DSF_OUT(DSF);
	REBVAL *value;
	REBCNT word_index;

	Eval_Functions++;

	// Copy stack frame variables as the closure object.  The +1 is for
	// SELF, as the REB_END is already accounted for by Make_Blk.

	frame = Make_Array(DSF->num_vars + 1);
	value = BLK_HEAD(frame);

	assert(DSF->num_vars == VAL_FUNC_NUM_PARAMS(func));

	SET_FRAME(value, NULL, VAL_FUNC_PARAMLIST(func));
	value++;

	for (word_index = 1; word_index <= DSF->num_vars; word_index++)
		*value++ = *DSF_VAR(DSF, word_index);

	frame->tail = word_index;
	TERM_SERIES(frame);

	// We do not Manage_Frame, because we are reusing a word series here
	// that has already been managed...only manage the outer series
	ASSERT_SERIES_MANAGED(FRM_KEYLIST(frame));
	MANAGE_SERIES(frame);

	ASSERT_FRAME(frame);

	// !!! For *today*, no option for function/closure to have a SELF
	// referring to their function or closure values.
	assert(VAL_TYPESET_SYM(BLK_HEAD(VAL_FUNC_PARAMLIST(func))) == SYM_0);

	// Clone the body of the closure to allow us to rebind words inside
	// of it so that they point specifically to the instances for this
	// invocation.  (Costly, but that is the mechanics of words.)
	//
	body = Copy_Array_Deep_Managed(VAL_FUNC_BODY(func));
	Rebind_Block(VAL_FUNC_PARAMLIST(func), frame, BLK_HEAD(body), REBIND_TYPE);

	// Protect the body from garbage collection during the course of the
	// execution.  (We could also protect it by stowing it in the call
	// frame's copy of the closure value, which we might think of as its
	// "archetype", but it may be valuable to keep that as-is.)
	PUSH_GUARD_SERIES(body);

	if (Do_Block_Throws(out, body, 0)) {
		if (
			IS_WORD(out) &&
			(VAL_WORD_SYM(out) == SYM_RETURN || VAL_WORD_SYM(out) == SYM_EXIT)
		) {
			if (!VAL_GET_EXT(func, EXT_FUNC_TRANSPARENT))
				TAKE_THROWN_ARG(out, out);
		}
	}

	// References to parts of the closure's copied body may still be
	// extant, but we no longer need to hold this reference on it
	DROP_GUARD_SERIES(body);
}


/***********************************************************************
**
*/	void Do_Routine(const REBVAL *routine)
/*
 */
{
	//RL_Print("%s, %d\n", __func__, __LINE__);
	REBSER *args = Copy_Values_Len_Shallow(
		DSF_NUM_ARGS(DSF) > 0 ? DSF_ARG(DSF, 1) : NULL,
		DSF_NUM_ARGS(DSF)
	);
	assert(VAL_FUNC_NUM_PARAMS(routine) == DSF_NUM_ARGS(DSF));

	Call_Routine(routine, args, DSF_OUT(DSF));

	Free_Series(args);
}
