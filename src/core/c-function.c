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
	REBSER *block;
	REBSER *words = VAL_FUNC_WORDS(func);
	REBCNT n;
	REBVAL *value;
	REBVAL *word;

	block = Make_Block(SERIES_TAIL(words));
	word = BLK_SKIP(words, 1);

	for (n = 1; n < SERIES_TAIL(words); word++, n++) {
		value = Alloc_Tail_Blk(block);
		VAL_SET(value, VAL_TYPE(word));
		VAL_WORD_SYM(value) = VAL_BIND_SYM(word);
		UNBIND(value);
	}

	return block;
}


/***********************************************************************
**
*/	REBSER *List_Func_Types(REBVAL *func)
/*
**		Return a block of function arg types.
**		Note: skips 0th entry.
**
***********************************************************************/
{
	REBSER *block;
	REBSER *words = VAL_FUNC_WORDS(func);
	REBCNT n;
	REBVAL *value;
	REBVAL *word;

	block = Make_Block(SERIES_TAIL(words));
	word = BLK_SKIP(words, 1);

	for (n = 1; n < SERIES_TAIL(words); word++, n++) {
		value = Alloc_Tail_Blk(block);
		VAL_SET(value, VAL_TYPE(word));
		VAL_WORD_SYM(value) = VAL_BIND_SYM(word);
		UNBIND(value);
	}

	return block;
}


/***********************************************************************
**
*/	REBSER *Check_Func_Spec(REBSER *block, REBYTE *exts)
/*
**		Check function spec of the form:
**
**		["description" arg "notes" [type! type2! ...] /ref ...]
**
**		Throw an error for invalid values.
**
***********************************************************************/
{
	REBVAL *blk;
	REBSER *words;
	REBINT n = 0;
	REBVAL *value;

	*exts = 0;

	blk = BLK_HEAD(block);
	words = Collect_Frame(BIND_ALL | BIND_NO_DUP | BIND_NO_SELF, 0, blk);

	// !!! needs more checks
	for (; NOT_END(blk); blk++) {
		switch (VAL_TYPE(blk)) {
		case REB_BLOCK:
			if (n == 0) {
				// !!! Rebol2 had the ability to put a block in the first
				// slot before any parameters, in which you could put words.
				// This is deprecated in favor of the use of tags.  For
				// @rgchris's sake we will allow the one element [catch]
				// block, during Rebol2 => Rebol3 migration.

				if (VAL_BLK_LEN(blk) == 1) {
					REBVAL *attribute = VAL_BLK_DATA(blk);
					if (
						IS_WORD(attribute)
						&& VAL_WORD_SYM(attribute) == SYM_CATCH
					) {
						break; // exempt, just ignore it
					}
				}
				Trap1_DEAD_END(RE_BAD_FUNC_DEF, blk);
			}

			// Turn block into typeset for parameter at current index
			Make_Typeset(VAL_BLK(blk), BLK_SKIP(words, n), 0);
			break;

		case REB_STRING:
		case REB_INTEGER:	// special case used by datatype test actions
			break;

		case REB_WORD:
		case REB_GET_WORD:
		case REB_LIT_WORD:
			n++;
			break;

		case REB_REFINEMENT:
			// Refinement only allows logic! and none! for its datatype:
			n++;
			value = BLK_SKIP(words, n);
			VAL_TYPESET(value) = (TYPESET(REB_LOGIC) | TYPESET(REB_NONE));
			break;

		case REB_TAG:
			// Tags are used to specify some EXT_FUNC opts switches.  At
			// present they are only allowed at the head of the spec block,
			// to try and keep things in at least a slightly canon format.
			// This may or may not be relaxed in the future.
			if (n != 0) Trap1_DEAD_END(RE_BAD_FUNC_DEF, blk);

			if (0 == Compare_String_Vals(blk, ROOT_INFIX_TAG, TRUE))
				SET_FLAG(*exts, EXT_FUNC_INFIX);
			else if (0 == Compare_String_Vals(blk, ROOT_TRANSPARENT_TAG, TRUE))
				SET_FLAG(*exts, EXT_FUNC_TRANSPARENT);
			else
				Trap1_DEAD_END(RE_BAD_FUNC_DEF, blk);
			break;

		case REB_SET_WORD:
		default:
			Trap1_DEAD_END(RE_BAD_FUNC_DEF, blk);
		}
	}

	return words;
}


/***********************************************************************
**
*/	void Make_Native(REBVAL *value, REBSER *spec, REBFUN func, REBINT type)
/*
***********************************************************************/
{
	REBYTE exts;
	//Print("Make_Native: %s spec %d", Get_Sym_Name(type+1), SERIES_TAIL(spec));
	VAL_FUNC_SPEC(value) = spec;
	VAL_FUNC_WORDS(value) = Check_Func_Spec(spec, &exts);

	// We don't expect special flags on natives like <transparent>, <infix>
	assert(exts == 0);

	VAL_FUNC_CODE(value) = func;
	VAL_SET(value, type);
}


/***********************************************************************
**
*/	REBFLG Make_Function(REBCNT type, REBVAL *value, REBVAL *def)
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
		|| !IS_BLOCK(spec = VAL_BLK(def))
	) return FALSE;

	body = VAL_BLK_SKIP(def, 1);

	VAL_FUNC_SPEC(value) = VAL_SERIES(spec);
	VAL_FUNC_WORDS(value) = Check_Func_Spec(VAL_SERIES(spec), &exts);

	if (type != REB_COMMAND) {
		if (len != 2 || !IS_BLOCK(body)) return FALSE;
		VAL_FUNC_BODY(value) = VAL_SERIES(body);
	}
	else
		Make_Command(value, def);

	VAL_SET(value, type); // clears exts and opts in header...
	VAL_EXTS_DATA(value) = exts; // ...so we set this after that point

	if (type == REB_FUNCTION || type == REB_CLOSURE)
		Bind_Relative(VAL_FUNC_WORDS(value), VAL_FUNC_WORDS(value), VAL_FUNC_BODY(value));

	return TRUE;
}


/***********************************************************************
**
*/	REBFLG Copy_Function(REBVAL *value, REBVAL *args)
/*
***********************************************************************/
{
	REBVAL *spec;
	REBVAL *body;

	if (!args || ((spec = VAL_BLK(args)) && IS_END(spec))) {
		body = 0;
		if (IS_FUNCTION(value) || IS_CLOSURE(value))
			VAL_FUNC_WORDS(value) = Copy_Block(VAL_FUNC_WORDS(value), 0);
	} else {
		body = VAL_BLK_SKIP(args, 1);
		// Spec given, must be block or *
		if (IS_BLOCK(spec)) {
			REBYTE exts;
			VAL_FUNC_SPEC(value) = VAL_SERIES(spec);
			VAL_FUNC_WORDS(value) = Check_Func_Spec(VAL_SERIES(spec), &exts);

			// !!! This feature seems to be tied to old make function
			// tricks that should likely be deleted instead of moved forward
			// with the new EXTS options...
			assert(exts == 0);
		} else {
			if (!IS_STAR(spec)) return FALSE;
			VAL_FUNC_WORDS(value) = Copy_Block(VAL_FUNC_WORDS(value), 0);
		}
	}

	if (body && !IS_END(body)) {
		if (!IS_FUNCTION(value) && !IS_CLOSURE(value)) return FALSE;
		// Body must be block:
		if (!IS_BLOCK(body)) return FALSE;
		VAL_FUNC_BODY(value) = VAL_SERIES(body);
	}
	// No body, use prototype:
	else if (IS_FUNCTION(value) || IS_CLOSURE(value))
		VAL_FUNC_BODY(value) = Clone_Block(VAL_FUNC_BODY(value));

	// Rebind function words:
	if (IS_FUNCTION(value) || IS_CLOSURE(value))
		Bind_Relative(VAL_FUNC_WORDS(value), VAL_FUNC_WORDS(value), VAL_FUNC_BODY(value));

	return TRUE;
}


/***********************************************************************
**
*/	void Clone_Function(REBVAL *value, REBVAL *func)
/*
***********************************************************************/
{
	REBSER *src_frame = VAL_FUNC_WORDS(func);

	VAL_FUNC_SPEC(value) = VAL_FUNC_SPEC(func);
	VAL_FUNC_BODY(value) = Clone_Block(VAL_FUNC_BODY(func));
	VAL_FUNC_WORDS(value) = Copy_Block(src_frame, 0);
	// VAL_FUNC_BODY(value) = Clone_Block(VAL_FUNC_BODY(func));
	VAL_FUNC_BODY(value) = Copy_Block_Values(VAL_FUNC_BODY(func), 0, SERIES_TAIL(VAL_FUNC_BODY(func)), TS_CLONE);
	Rebind_Block(src_frame, VAL_FUNC_WORDS(value), BLK_HEAD(VAL_FUNC_BODY(value)), 0);
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
	if (!action) Trap_Action(type, VAL_FUNC_ACT(func));
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

	if (DO_BLOCK_THROWS(out, VAL_FUNC_BODY(func), 0)) {
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
	//DISABLE_GC;

	// Clone the body of the closure to allow us to rebind words inside
	// of it so that they point specifically to the instances for this
	// invocation.  (Costly, but that is the mechanics of words.)
	body = Clone_Block(VAL_FUNC_BODY(func));

	// Copy stack frame variables as the closure object.  The +1 is for
	// SELF, as the REB_END is already accounted for by Make_Blk.

	frame = Make_Block(DSF->num_vars + 1);
	value = BLK_HEAD(frame);

	assert(DSF->num_vars == VAL_FUNC_NUM_WORDS(func));

	SET_FRAME(value, NULL, VAL_FUNC_WORDS(func));
	value++;

	for (word_index = 1; word_index <= DSF->num_vars; word_index++)
		*value++ = *DSF_VAR(DSF, word_index);

	frame->tail = word_index;
	TERM_SERIES(frame);
	ASSERT_FRAME(frame);

	// !!! For *today*, no option for function/closure to have a SELF
	// referring to their function or closure values.
	assert(VAL_WORD_SYM(BLK_HEAD(VAL_FUNC_WORDS(func))) == SYM_NOT_USED);

	// Rebind the body to the new context (deeply):
	Rebind_Block(VAL_FUNC_WORDS(func), frame, BLK_HEAD(body), REBIND_TYPE);

	SAVE_SERIES(body);
	if (DO_BLOCK_THROWS(out, body, 0)) {
		if (
			IS_WORD(out) &&
			(VAL_WORD_SYM(out) == SYM_RETURN || VAL_WORD_SYM(out) == SYM_EXIT)
		) {
			if (!VAL_GET_EXT(func, EXT_FUNC_TRANSPARENT))
				TAKE_THROWN_ARG(out, out);
		}
	}
	UNSAVE_SERIES(body);
}

/***********************************************************************
**
*/	void Do_Routine(const REBVAL *routine)
/*
 */
{
	//RL_Print("%s, %d\n", __func__, __LINE__);
	REBSER *args = Copy_Values(
		DSF_ARG(DSF, 1), SERIES_TAIL(VAL_FUNC_WORDS(routine)) - 1
	);
	Call_Routine(routine, args, DSF_OUT(DSF));
}
