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
**  Module:  c-frame.c
**  Summary: frame management
**  Section: core
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/
/*
		This structure is used for:

			1. Modules
			2. Objects
			3. Function frame (arguments)
			4. Closures

		A frame is a block that begins with a special FRAME! value
		(a datatype that links to the frame word list). That value
		(SELF) is followed by the values of the words for the frame.

		FRAME BLOCK:                            WORD LIST:
		+----------------------------+          +----------------------------+
		|    Frame Datatype Value    |--Series->|         SELF word          |
		+----------------------------+          +----------------------------+
		|          Value 1           |          |          Word 1            |
		+----------------------------+          +----------------------------+
		|          Value 2           |          |          Word 2            |
		+----------------------------+          +----------------------------+
		|          Value ...         |          |          Word ...          |
		+----------------------------+          +----------------------------+

		The word list holds word datatype values of the structure:

				Type:   word, 'word, :word, word:, /word
				Symbol: actual symbol
				Canon:  canonical symbol
				Typeset: index of the value's typeset, or zero

		This list is used for binding, evaluation, type checking, and
		can also be used for molding.

		When a frame is cloned, only the value block itself need be
		created. The word list remains the same. For functions, the
		value block can be pushed on the stack.

		Frame creation patterns:

			1. Function specification to frame. Spec is scanned for
			words and datatypes, from which the word list is created.
			Closures are identical.

			2. Object specification to frame. Spec is scanned for
			word definitions and merged with parent defintions. An
			option is to allow the words to be typed.

			3. Module words to frame. They are not normally known in
			advance, they are collected during the global binding of a
			newly loaded block. This requires either preallocation of
			the module frame, or some kind of special scan to track
			the new words.

			4. Special frames, such as system natives and actions
			may be created by specific block scans and appending to
			a given frame.
*/

#include "sys-core.h"

#define CHECK_BIND_TABLE

/***********************************************************************
**
*/	void Check_Bind_Table(void)
/*
***********************************************************************/
{
	REBCNT	n;
	REBINT *binds = WORDS_HEAD(Bind_Table);

	//Debug_Fmt("Bind Table (Size: %d)", SERIES_TAIL(Bind_Table));
	for (n = 0; n < SERIES_TAIL(Bind_Table); n++) {
		if (binds[n]) {
			Debug_Fmt("Bind table fault: %3d to %3d (%s)", n, binds[n], Get_Sym_Name(n));
		}
	}
}

/***********************************************************************
**
*/  REBSER *Make_Frame(REBINT len, REBOOL has_self)
/*
**      Create a frame of a given size, allocating space for both
**		words and values. Normally used for global frames.
**
***********************************************************************/
{
	REBSER *frame;
	REBSER *words;
	REBVAL *value;

	words = Make_Array(len + 1); // size + room for SELF
	frame = Make_Array(len + 1);

	// Note: cannot use Append_Frame for first word.
	value = Alloc_Tail_Array(frame);
	SET_FRAME(value, 0, words);
	value = Alloc_Tail_Array(words);
	Val_Init_Word_Typed(
		value, REB_WORD, has_self ? SYM_SELF : SYM_NOT_USED, ALL_64
	);

	return frame;
}


/***********************************************************************
**
*/  void Expand_Frame(REBSER *frame, REBCNT delta, REBCNT copy)
/*
**      Expand a frame. Copy words if flagged.
**
***********************************************************************/
{
	REBSER *words = FRM_WORD_SERIES(frame);

	Extend_Series(frame, delta);
	BLK_TERM(frame);

	// Expand or copy WORDS block:
	if (copy) {
		REBOOL managed = SERIES_GET_FLAG(FRM_WORD_SERIES(frame), SER_MANAGED);
		FRM_WORD_SERIES(frame) = Copy_Array_Extra_Shallow(words, delta);
		if (managed) MANAGE_SERIES(FRM_WORD_SERIES(frame));
	}
	else {
		Extend_Series(words, delta);
		BLK_TERM(words);
	}
}


/***********************************************************************
**
*/  REBVAL *Append_Frame(REBSER *frame, REBVAL *word, REBCNT sym)
/*
**      Append a word to the frame word list. Expands the list
**      if necessary. Returns the value cell for the word. (Set to
**      UNSET by default to avoid GC corruption.)
**
**      If word is not NULL, use the word sym and bind the word value,
**      otherwise use sym.
**
***********************************************************************/
{
	REBSER *words = FRM_WORD_SERIES(frame);
	REBVAL *value;

	// Add to word list:
	EXPAND_SERIES_TAIL(words, 1);
	value = BLK_LAST(words);
	Val_Init_Word_Typed(value, REB_WORD, word ? VAL_WORD_SYM(word) : sym, ALL_64);
	BLK_TERM(words);

	// Bind the word to this frame:
	if (word) {
		VAL_WORD_FRAME(word) = frame;
		VAL_WORD_INDEX(word) = frame->tail;
	}

	// Add unset value to frame:
	EXPAND_SERIES_TAIL(frame, 1);
	word = BLK_LAST(frame);
	SET_UNSET(word);
	BLK_TERM(frame);

	return word; // The value cell for word.
}


/***********************************************************************
**
*/  void Collect_Start(REBCNT modes)
/*
**		Use the Bind_Table to start collecting new words for
**		a frame. Use Collect_End() when done.
**
**		WARNING: Do not call code that might call BIND or otherwise
**		make use of the Bind_Table or the Word cache array (BUF_WORDS).
**
***********************************************************************/
{
	REBINT *binds = WORDS_HEAD(Bind_Table); // GC safe to do here

	CHECK_BIND_TABLE;

	// Reuse a global word list block because length of block cannot
	// be known until all words are scanned. Then copy this block.
	if (SERIES_TAIL(BUF_WORDS)) Panic(RP_WORD_LIST); // still in use

	// Add the SELF word to slot zero.
	if ((modes = (modes & BIND_NO_SELF)?0:SYM_SELF))
		binds[modes] = -1;  // (cannot use zero here)
	Val_Init_Word_Typed(BLK_HEAD(BUF_WORDS), REB_WORD, modes, ALL_64);
	SERIES_TAIL(BUF_WORDS) = 1;
}


/***********************************************************************
**
*/  REBSER *Collect_End(REBSER *prior)
/*
**		Finish collecting words, and free the Bind_Table for reuse.
**
***********************************************************************/
{
	REBVAL *words;
	REBINT *binds = WORDS_HEAD(Bind_Table); // GC safe to do here

	// Reset binding table (note BUF_WORDS may have expanded):
	for (words = BLK_HEAD(BUF_WORDS); NOT_END(words); words++)
		binds[VAL_WORD_CANON(words)] = 0;

	// If no new words, prior frame:
	if (prior && SERIES_TAIL(BUF_WORDS) == SERIES_TAIL(prior)) {
		RESET_TAIL(BUF_WORDS);  // allow reuse
		return FRM_WORD_SERIES(prior);
	}

	prior = Copy_Array_Shallow(BUF_WORDS);
	RESET_TAIL(BUF_WORDS);  // allow reuse

	CHECK_BIND_TABLE;

	return prior;
}


/***********************************************************************
**
*/  void Collect_Object(REBSER *prior)
/*
**		Collect words from a prior object.
**
***********************************************************************/
{
	REBVAL *words = FRM_WORDS(prior);
	REBINT *binds = WORDS_HEAD(Bind_Table);
	REBINT n;

	// this is necessary for memcpy below to not overwrite memory
	// BUF_WORDS does not own
	RESIZE_SERIES(BUF_WORDS, SERIES_TAIL(prior));

	// Word values can be copied just as bits (these are EXT_WORD_TYPED)
	memcpy(BLK_HEAD(BUF_WORDS), words, SERIES_TAIL(prior) * sizeof(REBVAL));

	SERIES_TAIL(BUF_WORDS) = SERIES_TAIL(prior);
	for (n = 1, words++; NOT_END(words); words++) // skips first = SELF
		binds[VAL_WORD_CANON(words)] = n++;
}


/***********************************************************************
**
*/ static void Collect_Frame_Inner_Loop(REBINT *binds, REBVAL value[], REBCNT modes)
/*
**		The inner recursive loop used for Collect_Frame function below.
**
***********************************************************************/
{
	for (; NOT_END(value); value++) {
		if (ANY_WORD(value)) {
			if (!binds[VAL_WORD_CANON(value)]) {  // only once per word
				if (IS_SET_WORD(value) || modes & BIND_ALL) {
					REBVAL *word;
					binds[VAL_WORD_CANON(value)] = SERIES_TAIL(BUF_WORDS);
					EXPAND_SERIES_TAIL(BUF_WORDS, 1);
					word = BLK_LAST(BUF_WORDS);
					Val_Init_Word_Typed(
						word,
						VAL_TYPE(value),
						VAL_WORD_SYM(value),
						// Allow all datatypes but END or UNSET (initially):
						~((TYPESET(REB_END) | TYPESET(REB_UNSET)))
					);
				}
			} else {
				// If word duplicated:
				if (modes & BIND_NO_DUP) {
					// Reset binding table (note BUF_WORDS may have expanded):
					REBVAL *word;
					for (word = BLK_HEAD(BUF_WORDS); NOT_END(word); word++)
						binds[VAL_WORD_CANON(word)] = 0;
					RESET_TAIL(BUF_WORDS);  // allow reuse
					Trap1(RE_DUP_VARS, value);
				}
			}
			continue;
		}
		// Recurse into sub-blocks:
		if (ANY_EVAL_BLOCK(value) && (modes & BIND_DEEP))
			Collect_Frame_Inner_Loop(binds, VAL_BLK_DATA(value), modes);
		// In this mode (foreach native), do not allow non-words:
		//else if (modes & BIND_GET) Trap_Arg_DEAD_END(value);
	}
	BLK_TERM(BUF_WORDS);
}


/***********************************************************************
**
*/  REBSER *Collect_Frame(REBSER *prior, REBVAL value[], REBCNT modes)
/*
**		Scans a block for words to use in the frame. The list of
**		words can then be used to create a frame. The Bind_Table is
**		used to quickly determine duplicate entries.
**
**		Returns:
**			A block of words that can be used for a frame word list.
**			If no new words, the prior list is returned.
**
**		Modes:
**			BIND_ALL  - scan all words, or just set words
**			BIND_DEEP - scan sub-blocks too
**			BIND_GET  - substitute :word with actual word
**			BIND_NO_SELF - do not add implicit SELF to the frame
**
***********************************************************************/
{
	Collect_Start(modes);

	// Setup binding table with existing words:
	if (prior) Collect_Object(prior);

	// Scan for words, adding them to BUF_WORDS and bind table:
	Collect_Frame_Inner_Loop(WORDS_HEAD(Bind_Table), &value[0], modes);

	return Collect_End(prior);
}


/***********************************************************************
**
*/  static void Collect_Words_Inner_Loop(REBINT *binds, REBVAL value[], REBCNT modes)
/*
**		Used for Collect_Words() after the binds table has
**		been set up.
**
***********************************************************************/
{
	for (; NOT_END(value); value++) {
		if (ANY_WORD(value)
			&& !binds[VAL_WORD_CANON(value)]
			&& (modes & BIND_ALL || IS_SET_WORD(value))
		) {
			REBVAL *word;
			binds[VAL_WORD_CANON(value)] = 1;
			word = Alloc_Tail_Array(BUF_WORDS);
			Val_Init_Word_Unbound(word, REB_WORD, VAL_WORD_SYM(value));
		}
		else if (ANY_EVAL_BLOCK(value) && (modes & BIND_DEEP))
			Collect_Words_Inner_Loop(binds, VAL_BLK_DATA(value), modes);
	}
}


/***********************************************************************
**
*/  REBSER *Collect_Words(REBVAL value[], REBVAL prior_value[], REBCNT modes)
/*
**		Collect words from a prior block and new block.
**
***********************************************************************/
{
	REBSER *series;
	REBCNT start;
	REBINT *binds = WORDS_HEAD(Bind_Table); // GC safe to do here
	CHECK_BIND_TABLE;

	if (SERIES_TAIL(BUF_WORDS)) Panic_DEAD_END(RP_WORD_LIST); // still in use

	if (prior_value)
		Collect_Words_Inner_Loop(binds, &prior_value[0], BIND_ALL);

	start = SERIES_TAIL(BUF_WORDS);
	Collect_Words_Inner_Loop(binds, &value[0], modes);

	// Reset word markers:
	for (value = BLK_HEAD(BUF_WORDS); NOT_END(value); value++)
		binds[VAL_WORD_CANON(value)] = 0;

	series = Copy_Array_At_Max_Shallow(
		BUF_WORDS, start, SERIES_TAIL(BUF_WORDS) - start
	);
	RESET_TAIL(BUF_WORDS);  // allow reuse

	CHECK_BIND_TABLE;
	return series;
}


/***********************************************************************
**
*/  REBSER *Create_Frame(REBSER *words, REBSER *spec)
/*
**      Create a new frame from a word list.
**      The values of the frame are initialized to NONE.
**
***********************************************************************/
{
	REBINT len = SERIES_TAIL(words);
	REBSER *frame = Make_Array(len);
	REBVAL *value = BLK_HEAD(frame);

	SET_FRAME(value, spec, words);

	SERIES_TAIL(frame) = len;
	for (value++, len--; len > 0; len--, value++) SET_NONE(value); // skip first value (self)
	SET_END(value);

	return frame;
}


/***********************************************************************
**
*/  void Rebind_Frame(REBSER *src_frame, REBSER *dst_frame)
/*
**      Clone old src_frame to new dst_frame knowing
**		which types of values need to be copied, deep copied, and rebound.
**
***********************************************************************/
{
	// Rebind all values:
	Rebind_Block(src_frame, dst_frame, BLK_SKIP(dst_frame, 1), REBIND_FUNC);
}


/***********************************************************************
**
*/  REBSER *Make_Object(REBSER *parent, REBVAL value[])
/*
**      Create an object from a parent object and a spec block.
**		The words within the resultant object are not bound.
**
***********************************************************************/
{
	REBSER *words;
	REBSER *object;

	PG_Reb_Stats->Objects++;

	if (!value || IS_END(value)) {
		if (parent) {
			object = Copy_Array_Core_Managed(
				parent, 0, SERIES_TAIL(parent), TRUE, TS_CLONE
			);
		}
		else {
			object = Make_Frame(0, TRUE);
			MANAGE_FRAME(object);
		}
	}
	else {
		words = Collect_Frame(parent, &value[0], BIND_ONLY); // GC safe
		object = Create_Frame(words, 0); // GC safe
		if (parent) {
			if (Reb_Opts->watch_obj_copy)
				Debug_Fmt(cs_cast(BOOT_STR(RS_WATCH, 2)), SERIES_TAIL(parent) - 1, FRM_WORD_SERIES(object));

			// Bitwise copy parent values (will have bits fixed by Clonify)
			memcpy(
				FRM_VALUES(object) + 1,
				FRM_VALUES(parent) + 1,
				(SERIES_TAIL(parent) - 1) * sizeof(REBVAL)
			);

			// For values we copied that were blocks and strings, replace
			// their series components with deep copies of themselves:
			Clonify_Values_Len_Managed(
				BLK_SKIP(object, 1), SERIES_TAIL(object) - 1, TRUE, TS_CLONE
			);

			// The *word series* might have been reused from the parent,
			// based on whether any words were added, or we could have gotten
			// a fresh one back.  Force our invariant here (as the screws
			// tighten...)
			ENSURE_SERIES_MANAGED(FRM_WORD_SERIES(object));
			MANAGE_SERIES(object);
		}
		else {
			MANAGE_FRAME(object);
		}

		assert(words == FRM_WORD_SERIES(object));
	}

	assert(SERIES_GET_FLAG(object, SER_MANAGED));
	assert(SERIES_GET_FLAG(FRM_WORD_SERIES(object), SER_MANAGED));
	ASSERT_FRAME(object);
	return object;
}


/***********************************************************************
**
*/  REBSER *Construct_Object(REBSER *parent, REBVAL value[], REBFLG as_is)
/*
**		Construct an object (partial evaluation of block).
**		Parent can be null. Values are rebound.
**
***********************************************************************/
{
	REBSER *frame = Make_Object(parent, &value[0]);

	if (NOT_END(value)) Bind_Values_Shallow(&value[0], frame);

	if (as_is) Do_Min_Construct(&value[0]);
	else Do_Construct(&value[0]);

	return frame;
}


/***********************************************************************
**
*/  REBSER *Make_Object_Block(REBSER *frame, REBINT mode)
/*
**      Return a block containing words, values, or set-word: value
**      pairs for the given object. Note: words are bound to original
**      object.
**
**      Modes:
**          1 for word
**          2 for value
**          3 for words and values
**
***********************************************************************/
{
	REBVAL *words  = FRM_WORDS(frame);
	REBVAL *values = FRM_VALUES(frame);
	REBSER *block;
	REBVAL *value;
	REBCNT n;

	n = (mode & 4) ? 0 : 1;
	block = Make_Array(SERIES_TAIL(frame) * (n + 1));

	for (; n < SERIES_TAIL(frame); n++) {
		if (!VAL_GET_EXT(words + n, EXT_WORD_HIDE)) {
			if (mode & 1) {
				value = Alloc_Tail_Array(block);
				if (mode & 2) {
					VAL_SET(value, REB_SET_WORD);
					VAL_SET_OPT(value, OPT_VALUE_LINE);
				}
				else VAL_SET(value, REB_WORD); //VAL_TYPE(words+n));
				VAL_WORD_SYM(value) = VAL_BIND_SYM(words+n);
				VAL_WORD_INDEX(value) = n;
				VAL_WORD_FRAME(value) = frame;
			}
			if (mode & 2) {
				Append_Value(block, values+n);
			}
		}
	}

	return block;
}


/***********************************************************************
**
*/	void Assert_Public_Object(const REBVAL *value)
/*
***********************************************************************/
{
	REBVAL *word  = BLK_HEAD(VAL_OBJ_WORDS(value));

	for (; NOT_END(word); word++)
		if (VAL_GET_EXT(word, EXT_WORD_HIDE)) Trap(RE_HIDDEN);
}


/***********************************************************************
**
*/	void Make_Module(REBVAL *out, const REBVAL *spec)
/*
**      Create a module from a spec and an init block.
**		Call the Make_Module function in the system/intrinsic object.
**
***********************************************************************/
{
	if (!Do_Sys_Func(out, SYS_CTX_MAKE_MODULE_P, spec, 0)) {
		// Gave back an unhandled RETURN, BREAK, CONTINUE, etc...
		Trap_Thrown(out);
		DEAD_END_VOID;
	}

	// !!! Shouldn't this be testing for !IS_MODULE(out)?
	if (IS_NONE(out)) Trap1(RE_INVALID_SPEC, spec);
}


/***********************************************************************
**
*/  REBSER *Make_Module_Spec(REBVAL *spec)
/*
**		Create a module spec object. Holds module name, version,
**		exports, locals, and more. See system/standard/module.
**
***********************************************************************/
{
	// Build standard module header object:
	REBSER *obj = VAL_OBJ_FRAME(Get_System(SYS_STANDARD, STD_SCRIPT));
	REBSER *frame;

	if (spec && IS_BLOCK(spec))
		frame = Construct_Object(obj, VAL_BLK_DATA(spec), FALSE);
	else
		frame = Copy_Array_Shallow(obj);

	return frame;
}


/***********************************************************************
**
*/  REBSER *Merge_Frames(REBSER *parent1, REBSER *parent2)
/*
**      Create a child frame from two parent frames. Merge common fields.
**      Values from the second parent take precedence.
**
**		Deep copy and rebind the child.
**
***********************************************************************/
{
	REBSER *wrds;
	REBSER *child;
	REBVAL *words;
	REBVAL *value;
	REBCNT n;
	REBINT *binds = WORDS_HEAD(Bind_Table);

	// Merge parent1 and parent2 words.
	// Keep the binding table.
	Collect_Start(BIND_ALL);
	// Setup binding table and BUF_WORDS with parent1 words:
	if (parent1) Collect_Object(parent1);
	// Add parent2 words to binding table and BUF_WORDS:
	Collect_Frame_Inner_Loop(
		binds, BLK_SKIP(FRM_WORD_SERIES(parent2), 1), BIND_ALL
	);

	// Allocate child (now that we know the correct size):
	wrds = Copy_Array_Shallow(BUF_WORDS);
	child = Make_Array(SERIES_TAIL(wrds));
	value = Alloc_Tail_Array(child);
	VAL_SET(value, REB_FRAME);
	VAL_FRM_WORDS(value) = wrds;
	VAL_FRM_SPEC(value) = 0;

	// Copy parent1 values:
	memcpy(
		FRM_VALUES(child) + 1,
		FRM_VALUES(parent1) + 1,
		(SERIES_TAIL(parent1) - 1) * sizeof(REBVAL)
	);

	// Copy parent2 values:
	words = FRM_WORDS(parent2)+1;
	value = FRM_VALUES(parent2)+1;
	for (; NOT_END(words); words++, value++) {
		// no need to search when the binding table is available
		n = binds[VAL_WORD_CANON(words)];
		BLK_HEAD(child)[n] = *value;
	}

	// Terminate the child frame:
	SERIES_TAIL(child) = SERIES_TAIL(wrds);
	BLK_TERM(child);

	// Deep copy the child
	Clonify_Values_Len_Managed(
		BLK_SKIP(child, 1), SERIES_TAIL(child) - 1, TRUE, TS_CLONE
	);

	// Rebind the child
	Rebind_Block(parent1, child, BLK_SKIP(child, 1), REBIND_FUNC);
	Rebind_Block(parent2, child, BLK_SKIP(child, 1), REBIND_FUNC | REBIND_TABLE);

	// release the bind table
	Collect_End(child);

	return child;
}


/***********************************************************************
**
*/	void Resolve_Context(REBSER *target, REBSER *source, REBVAL *only_words, REBFLG all, REBFLG expand)
/*
**		Only_words can be a block of words or an index in the target
**		(for new words).
**
***********************************************************************/
{
	REBINT *binds  = WORDS_HEAD(Bind_Table); // GC safe to do here
	REBVAL *words;
	REBVAL *vals;
	REBINT n;
	REBINT m;
	REBCNT i = 0;

	CHECK_BIND_TABLE;

	if (IS_PROTECT_SERIES(target)) Trap(RE_PROTECTED);

	if (IS_INTEGER(only_words)) { // Must be: 0 < i <= tail
		i = VAL_INT32(only_words); // never <= 0
		if (i == 0) i = 1;
		if (i >= target->tail) return;
	}

	Collect_Start(BIND_NO_SELF);  // DO NOT TRAP IN THIS SECTION

	n = 0;

	// If limited resolve, tag the word ids that need to be copied:
	if (i) {
		// Only the new words of the target:
		for (words = FRM_WORD(target, i); NOT_END(words); words++)
			binds[VAL_BIND_CANON(words)] = -1;
		n = SERIES_TAIL(target) - 1;
	}
	else if (IS_BLOCK(only_words)) {
		// Limit exports to only these words:
		for (words = VAL_BLK_DATA(only_words); NOT_END(words); words++) {
			if (IS_WORD(words) || IS_SET_WORD(words)) {
				binds[VAL_WORD_CANON(words)] = -1;
				n++;
			}
		}
	}

	// Expand target as needed:
	if (expand && n > 0) {
		// Determine how many new words to add:
		for (words = FRM_WORD(target, 1); NOT_END(words); words++)
			if (binds[VAL_BIND_CANON(words)]) n--;
		// Expand frame by the amount required:
		if (n > 0) Expand_Frame(target, n, 0);
		else expand = 0;
	}

	// Maps a word to its value index in the source context.
	// Done by marking all source words (in bind table):
	words = FRM_WORDS(source)+1;
	for (n = 1; NOT_END(words); n++, words++) {
		if (IS_NONE(only_words) || binds[VAL_BIND_CANON(words)])
			binds[VAL_WORD_CANON(words)] = n;
	}

	// Foreach word in target, copy the correct value from source:
	n = i ? i : 1;
	vals = FRM_VALUE(target, n);
	for (words = FRM_WORD(target, n); NOT_END(words); words++, vals++) {
		if ((m = binds[VAL_BIND_CANON(words)])) {
			binds[VAL_BIND_CANON(words)] = 0; // mark it as set
			if (
				!VAL_GET_EXT(words, EXT_WORD_LOCK)
				&& (all || IS_UNSET(vals))
			) {
				if (m < 0) SET_UNSET(vals); // no value in source context
				else *vals = *FRM_VALUE(source, m);
				//Debug_Num("type:", VAL_TYPE(vals));
				//Debug_Str(Get_Word_Name(words));
			}
		}
	}

	// Add any new words and values:
	if (expand) {
		REBVAL *val;
		words = FRM_WORDS(source)+1;
		for (n = 1; NOT_END(words); n++, words++) {
			if (binds[VAL_BIND_CANON(words)]) {
				// Note: no protect check is needed here
				binds[VAL_BIND_CANON(words)] = 0;
				val = Append_Frame(target, 0, VAL_BIND_SYM(words));
				*val = *FRM_VALUE(source, n);
			}
		}
	}
	else {
		// Reset bind table (do not use Collect_End):
		if (i) {
			for (words = FRM_WORD(target, i); NOT_END(words); words++)
				binds[VAL_BIND_CANON(words)] = 0;
		}
		else if (IS_BLOCK(only_words)) {
			for (words = VAL_BLK_DATA(only_words); NOT_END(words); words++) {
				if (IS_WORD(words) || IS_SET_WORD(words)) binds[VAL_WORD_CANON(words)] = 0;
			}
		}
		else {
			for (words = FRM_WORDS(source)+1; NOT_END(words); words++)
				binds[VAL_BIND_CANON(words)] = 0;
		}
	}

	CHECK_BIND_TABLE;

	RESET_TAIL(BUF_WORDS);  // allow reuse, trapping ok now
}


/***********************************************************************
**
*/  static void Bind_Values_Inner_Loop(REBINT *binds, REBVAL value[], REBSER *frame, REBCNT mode)
/*
**		Bind_Values_Core() sets up the binding table and then calls
**		this recursive routine to do the actual binding.
**
***********************************************************************/
{
	REBFLG selfish = !IS_SELFLESS(frame);

	for (; NOT_END(value); value++) {
		if (ANY_WORD(value)) {
			//Print("Word: %s", Get_Sym_Name(VAL_WORD_CANON(value)));
			// Is the word found in this frame?
			REBCNT n = binds[VAL_WORD_CANON(value)];
			if (n != 0) {
				if (n == NO_RESULT) n = 0; // SELF word
				assert(n < SERIES_TAIL(frame));
				// Word is in frame, bind it:
				VAL_WORD_INDEX(value) = n;
				VAL_WORD_FRAME(value) = frame;
			}
			else if (selfish && VAL_WORD_CANON(value) == SYM_SELF) {
				VAL_WORD_INDEX(value) = 0;
				VAL_WORD_FRAME(value) = frame;
			}
			else {
				// Word is not in frame. Add it if option is specified:
				if ((mode & BIND_ALL) || ((mode & BIND_SET) && (IS_SET_WORD(value)))) {
					Expand_Frame(frame, 1, 1);
					Append_Frame(frame, value, 0);
					binds[VAL_WORD_CANON(value)] = VAL_WORD_INDEX(value);
				}
			}
		}
		else if (ANY_BLOCK(value) && (mode & BIND_DEEP))
			Bind_Values_Inner_Loop(
				binds, VAL_BLK_DATA(value), frame, mode
			);
		else if ((IS_FUNCTION(value) || IS_CLOSURE(value)) && (mode & BIND_FUNC))
			Bind_Values_Inner_Loop(
				binds, BLK_HEAD(VAL_FUNC_BODY(value)), frame, mode
			);
	}
}


/***********************************************************************
**
*/  void Bind_Values_Core(REBVAL value[], REBSER *frame, REBCNT mode)
/*
**		Bind words in an array of values terminated with REB_END
**		to a specified frame.  See warnings on the functions like
**		Bind_Values_Deep() about not passing just a singular REBVAL.
**
**		Different modes may be applied:
**
**          BIND_ONLY - Only bind words found in the frame.
**          BIND_ALL  - Add words to the frame during the bind.
**          BIND_SET  - Add set-words to the frame during the bind.
**                      (note: word must not occur before the SET)
**          BIND_DEEP - Recurse into sub-blocks.
**
**		NOTE: BIND_SET must be used carefully, because it does not
**		bind prior instances of the word before the set-word. That is
**		to say that forward references are not allowed.
**
***********************************************************************/
{
	REBVAL *words;
	REBCNT index;
	REBINT *binds = WORDS_HEAD(Bind_Table); // GC safe to do here

	CHECK_MEMORY(4);

	CHECK_BIND_TABLE;

	// Note about optimization: it's not a big win to avoid the
	// binding table for short blocks (size < 4), because testing
	// every block for the rare case adds up.

	// Setup binding table
	for (index = 1; index < frame->tail; index++) {
		words = FRM_WORD(frame, index);
		if (!VAL_GET_OPT(words, EXT_WORD_HIDE))
			binds[VAL_BIND_CANON(words)] = index;
	}

	Bind_Values_Inner_Loop(binds, &value[0], frame, mode);

	// Reset binding table:
	for (words = FRM_WORDS(frame) + 1; NOT_END(words); words++)
		binds[VAL_BIND_CANON(words)] = 0;

	CHECK_BIND_TABLE;
}


/***********************************************************************
**
*/  void Unbind_Values_Core(REBVAL value[], REBSER *frame, REBOOL deep)
/*
**		Unbind words in a block, optionally unbinding those which are
**		bound to a particular frame (if frame is NULL, then all
**		words will be unbound regardless of their VAL_WORD_FRAME).
**
***********************************************************************/
{
	for (; NOT_END(value); value++) {
		if (ANY_WORD(value) && (!frame || VAL_WORD_FRAME(value) == frame))
			UNBIND_WORD(value);

		if (ANY_BLOCK(value) && deep)
			Unbind_Values_Core(VAL_BLK_DATA(value), frame, TRUE);
	}
}


/***********************************************************************
**
*/  REBCNT Bind_Word(REBSER *frame, REBVAL *word)
/*
**		Binds a word to a frame. If word is not part of the
**		frame, ignore it.
**
***********************************************************************/
{
	REBCNT n;

	n = Find_Word_Index(frame, VAL_WORD_SYM(word), FALSE);
	if (n != 0) {
		VAL_WORD_FRAME(word) = frame;
		VAL_WORD_INDEX(word) = n;
	}
	return n;
}


/***********************************************************************
**
*/  static void Bind_Relative_Inner_Loop(REBINT *binds, REBSER *frame, REBSER *block)
/*
**      Recursive function for relative function word binding.
**
**      Note: frame arg points to an identifying series of the function,
**      not a normal frame. This will be used to verify the word fetch.
**
***********************************************************************/
{
	REBVAL *value = BLK_HEAD(block);

	for (; NOT_END(value); value++) {
		if (ANY_WORD(value)) {
			// Is the word (canon sym) found in this frame?
			REBINT n = binds[VAL_WORD_CANON(value)];
			if (n != 0) {
				// Word is in frame, bind it:
				VAL_WORD_INDEX(value) = n;
				VAL_WORD_FRAME(value) = frame; // func body
			}
		}
		else if (ANY_BLOCK(value))
			Bind_Relative_Inner_Loop(binds, frame, VAL_SERIES(value));
	}
}


/***********************************************************************
**
*/  void Bind_Relative(REBSER *words, REBSER *frame, REBSER *block)
/*
**      Bind the words of a function block to a stack frame.
**      To indicate the relative nature of the index, it is set to
**		a negative offset.
**
**		words: VAL_FUNC_WORDS(func)
**		frame: VAL_FUNC_WORDS(func)
**		block: block to bind
**
***********************************************************************/
{
	REBVAL *args;
	REBINT index;
	REBINT *binds = WORDS_HEAD(Bind_Table); // GC safe to do here

	args = BLK_SKIP(words, 1);

	CHECK_BIND_TABLE;

	//Dump_Block(words);

	// Setup binding table from the argument word list:
	for (index = 1; NOT_END(args); args++, index++)
		binds[VAL_BIND_CANON(args)] = -index;

	Bind_Relative_Inner_Loop(binds, frame, block);

	// Reset binding table:
	for (args = BLK_SKIP(words, 1); NOT_END(args); args++)
		binds[VAL_BIND_CANON(args)] = 0;

	CHECK_BIND_TABLE;
}


/***********************************************************************
**
*/  void Bind_Stack_Block(REBSER *frame, REBSER *block)
/*
***********************************************************************/
{
	Bind_Relative(frame, frame, block);
}


/***********************************************************************
**
*/  void Bind_Stack_Word(REBSER *frame, REBVAL *word)
/*
***********************************************************************/
{
	REBINT index;

	index = Find_Arg_Index(frame, VAL_WORD_SYM(word));
	if (!index) Trap1(RE_NOT_IN_CONTEXT, word);
	VAL_WORD_FRAME(word) = frame;
	VAL_WORD_INDEX(word) = -index;
}


/***********************************************************************
**
*/  void Rebind_Block(REBSER *src_frame, REBSER *dst_frame, REBVAL *data, REBFLG modes)
/*
**      Rebind all words that reference src frame to dst frame.
**      Rebind is always deep.
**
**		There are two types of frames: relative frames and normal frames.
**		When frame_src type and frame_dst type differ,
**		modes must have REBIND_TYPE.
**
***********************************************************************/
{
	REBINT *binds = WORDS_HEAD(Bind_Table);

	for (; NOT_END(data); data++) {
		if (ANY_BLOCK(data))
			Rebind_Block(src_frame, dst_frame, VAL_BLK_DATA(data), modes);
		else if (ANY_WORD(data) && VAL_WORD_FRAME(data) == src_frame) {
			VAL_WORD_FRAME(data) = dst_frame;
			if (modes & REBIND_TABLE) VAL_WORD_INDEX(data) = binds[VAL_WORD_CANON(data)];
			if (modes & REBIND_TYPE) VAL_WORD_INDEX(data) = - VAL_WORD_INDEX(data);
		} else if ((modes & REBIND_FUNC) && (IS_FUNCTION(data) || IS_CLOSURE(data)))
			Rebind_Block(src_frame, dst_frame, BLK_HEAD(VAL_FUNC_BODY(data)), modes);
	}
}


/***********************************************************************
**
*/  REBCNT Find_Arg_Index(REBSER *args, REBCNT sym)
/*
**		Find function arg word in function arg "frame".
**
***********************************************************************/
{
	REBCNT n;
	REBCNT s;
	REBVAL *word;
	REBCNT len;

	s = SYMBOL_TO_CANON(sym); // always compare to CANON sym

	word = BLK_SKIP(args, 1);
	len = SERIES_TAIL(args);

	for (n = 1; n < len; n++, word++)
		if (sym == VAL_BIND_SYM(word) || s == VAL_BIND_CANON(word)) return n;

	return 0;
}


/***********************************************************************
**
*/  REBCNT Find_Word_Index(REBSER *frame, REBCNT sym, REBFLG always)
/*
**      Search a frame looking for the given word symbol.
**      Return the frame index for a word. Locate it by matching
**      the canon word identifiers. Return 0 if not found.
**
***********************************************************************/
{
	REBCNT len = SERIES_TAIL(FRM_WORD_SERIES(frame));
	REBVAL *word = FRM_WORDS(frame) + 1;
	REBCNT n;
	REBCNT s;

	s = SYMBOL_TO_CANON(sym); // always compare to CANON sym

	for (n = 1; n < len; n++, word++)
		if (sym == VAL_BIND_SYM(word) || s == VAL_BIND_CANON(word))
			return (!always && VAL_GET_EXT(word, EXT_WORD_HIDE)) ? 0 : n;

	return 0;
}


/***********************************************************************
**
*/  REBVAL *Find_Word_Value(REBSER *frame, REBCNT sym)
/*
**      Search a frame looking for the given word symbol and
**      return the value for the word. Locate it by matching
**      the canon word identifiers. Return NULL if not found.
**
***********************************************************************/
{
	REBINT n;

	if (!frame) return 0;
	n = Find_Word_Index(frame, sym, FALSE);
	if (n == 0) return 0;
	return BLK_SKIP(frame, n);
}


/***********************************************************************
**
*/	REBCNT Find_Word(REBSER *series, REBCNT index, REBCNT sym)
/*
**		Find word (of any type) in a block... quickly.
**
***********************************************************************/
{
	REBVAL *value;

	for (; index < SERIES_TAIL(series); index++) {
		value = BLK_SKIP(series, index);
		if (ANY_WORD(value) && sym == VAL_WORD_CANON(value))
			return index;
	}

	return NOT_FOUND;
}


/***********************************************************************
**
*/  REBVAL *Get_Var_Core(const REBVAL *word, REBOOL trap, REBOOL writable)
/*
**      Get the word--variable--value. (Generally, use the macros like
**      GET_VAR or GET_MUTABLE_VAR instead of this).  This routine is
**		called quite a lot and so attention to performance is important.
**
**      Coded assuming most common case is trap=TRUE and writable=FALSE
**
***********************************************************************/
{
	REBSER *context = VAL_WORD_FRAME(word);

	if (context) {
		REBINT index = VAL_WORD_INDEX(word);

		// POSITIVE INDEX: The word is bound directly to a value inside
		// a frame, and represents the zero-based offset into that series.
		// This is how values would be picked out of object-like things...
		// (Including looking up 'append' in the user context.)

		if (index > 0) {
			REBVAL *value;
			if (
				writable &&
				VAL_GET_EXT(FRM_WORDS(context) + index, EXT_WORD_LOCK)
			) {
				if (trap) {
					Trap1(RE_LOCKED_WORD, word);
					DEAD_END;
				}
				return NULL;
			}

			value = FRM_VALUES(context) + index;
			assert(!THROWN(value));
			return value;
		}

		// NEGATIVE INDEX: Word is stack-relative bound to a function with
		// no persistent frame held by the GC.  The value *might* be found
		// on the stack (or not, if all instances of the function on the
		// call stack have finished executing).  We walk backward in the call
		// stack to see if we can find the function's "identifying series"
		// in a call frame...and take the first instance we see (even if
		// multiple invocations are on the stack, most recent wins)

		if (index < 0) {
			struct Reb_Call *call = DSF;

			// Get_Var could theoretically be called with no evaluation on
			// the stack, so check for no DSF first...
			while (call) {
				if (
					call->args_ready
					&& context == VAL_FUNC_WORDS(DSF_FUNC(call))
				) {
					REBVAL *value;

					assert(!IS_CLOSURE(DSF_FUNC(call)));

					if (
						writable &&
						VAL_GET_EXT(
							VAL_FUNC_PARAM(DSF_FUNC(call), -index),
							EXT_WORD_LOCK
						)
					) {
						if (trap) {
							Trap1(RE_LOCKED_WORD, word);
							DEAD_END;
						}
						return NULL;
					}

					value = DSF_ARG(call, -index);
					assert(!THROWN(value));
					return value;
				}

				call = PRIOR_DSF(call);
			}

			if (trap) {
				Trap1(RE_NO_RELATIVE, word);
				DEAD_END;
			}
			return NULL;
		}

		// ZERO INDEX: The word is SELF.  Although the information needed
		// to produce an OBJECT!-style REBVAL lives in the zero offset
		// of the frame, it's not a value that we can return a direct
		// pointer to.  Use GET_VAR_INTO instead for that.

		assert(!IS_SELFLESS(context));
		if (trap) {
			Trap(RE_SELF_PROTECTED);
			DEAD_END;
		}
		return NULL; // is this a case where we should *always* trap?
	}

	if (trap) {
		Trap1(RE_NOT_DEFINED, word);
		DEAD_END;
	}
	return NULL;
}


/***********************************************************************
**
*/  void Get_Var_Into_Core(REBVAL *out, const REBVAL *word)
/*
**      Variant of Get_Var_Core that always traps and never returns a
**      direct pointer into a frame.  It is thus able to give back
**      `self` lookups, and doesn't have to check the word's protection
**      status before returning.
**
**      See comments in Get_Var_Core for what it's actually doing.
**
***********************************************************************/
{
	REBSER *context = VAL_WORD_FRAME(word);

	if (context) {
		REBINT index = VAL_WORD_INDEX(word);

		if (index > 0) {
			*out = *(FRM_VALUES(context) + index);
			assert(!IS_TRASH(out));
			assert(!THROWN(out));
			return;
		}

		if (index < 0) {
			struct Reb_Call *call = DSF;
			while (call) {
				if (
					call->args_ready
					&& context == VAL_FUNC_WORDS(DSF_FUNC(call))
				) {
					assert(!IS_CLOSURE(DSF_FUNC(call)));
					*out = *DSF_ARG(call, -index);
					assert(!IS_TRASH(out));
					assert(!THROWN(out));
					return;
				}
				call = PRIOR_DSF(call);
			}

			Trap1(RE_NO_RELATIVE, word);
			DEAD_END_VOID;
		}

		// Key difference between Get_Var_Into and Get_Var...fabricating
		// an object REBVAL.

		// !!! Could fake function frames stow the function value itself
		// so 'binding-of' can return it and use for binding (vs. TRUE)?

		assert(!IS_SELFLESS(context));
		Val_Init_Object(out, context);
		return;
	}

	Trap1(RE_NOT_DEFINED, word);
	DEAD_END_VOID;
}


/***********************************************************************
**
*/  void Set_Var(const REBVAL *word, const REBVAL *value)
/*
**      Set the word (variable) value. (Use macro when possible).
**
***********************************************************************/
{
	REBINT index = VAL_WORD_INDEX(word);
	struct Reb_Call *call;
	REBSER *frm;

	assert(!THROWN(value));

	if (!HAS_FRAME(word)) Trap1(RE_NOT_DEFINED, word);

	assert(VAL_WORD_FRAME(word));
//  Print("Set %s to %s [frame: %x idx: %d]", Get_Word_Name(word), Get_Type_Name(value), VAL_WORD_FRAME(word), VAL_WORD_INDEX(word));

	if (index > 0) {
		frm = VAL_WORD_FRAME(word);
		if (VAL_GET_EXT(FRM_WORDS(frm) + index, EXT_WORD_LOCK))
			Trap1(RE_LOCKED_WORD, word);
		FRM_VALUES(frm)[index] = *value;
		return;
	}
	if (index == 0) Trap(RE_SELF_PROTECTED);

	// Find relative value:
	call = DSF;
	while (VAL_WORD_FRAME(word) != VAL_WORD_FRAME(DSF_LABEL(call))) {
		call = PRIOR_DSF(call);
		if (!call) Trap1(RE_NOT_DEFINED, word); // change error !!!
	}
	*DSF_ARG(call, -index) = *value;
}


/***********************************************************************
**
*/	REBVAL *Obj_Word(const REBVAL *value, REBCNT index)
/*
**		Return pointer to the nth WORD of an object.
**
***********************************************************************/
{
	REBSER *obj = VAL_OBJ_WORDS(value);
	return BLK_SKIP(obj, index);
}


/***********************************************************************
**
*/	REBVAL *Obj_Value(REBVAL *value, REBCNT index)
/*
**		Return pointer to the nth VALUE of an object.
**		Return zero if the index is not valid.
**
***********************************************************************/
{
	REBSER *obj = VAL_OBJ_FRAME(value);

	if (index >= SERIES_TAIL(obj)) return 0;
	return BLK_SKIP(obj, index);
}


/***********************************************************************
**
*/  void Init_Obj_Value(REBVAL *value, REBSER *frame)
/*
***********************************************************************/
{
	assert(frame);
	CLEARS(value);
	Val_Init_Object(value, frame);
}


/***********************************************************************
**
*/	void Init_Frame(void)
/*
***********************************************************************/
{
	// Temporary block used while scanning for frame words:
	Set_Root_Series(TASK_BUF_WORDS, Make_Array(100), "word cache"); // just holds words, no GC
}


#ifndef NDEBUG
/***********************************************************************
**
*/  void Assert_Frame_Core(REBSER *frame)
/*
***********************************************************************/
{
	REBINT n;
	REBVAL *value;
	REBSER *words;
	REBVAL *word;
	REBINT tail;
	REBVAL *frame_value; // "FRAME!-typed value" at head of "frame" series

	frame_value = BLK_HEAD(frame);
	if (!IS_FRAME(frame_value)) Panic_Series(frame);

	if ((frame == VAL_SERIES(ROOT_ROOT)) || (frame == Task_Series)) {
		// !!! Currently it is allowed that the root frames not
		// have a wordlist.  This distinct behavior accomodation is
		// not worth having the variance of behavior, but since
		// it's there for now... allow it for just those two.

		if(!FRM_WORD_SERIES(frame))
			return;
	}

	value = FRM_VALUES(frame);

	words = FRM_WORD_SERIES(frame);
	word = FRM_WORDS(frame);
	tail = SERIES_TAIL(frame);

	for (n = 0; n < tail; n++, value++, word++) {
		if (n == 0) {
			if (
				VAL_WORD_SYM(word) != SYM_SELF
				&& VAL_WORD_SYM(word) != SYM_NOT_USED
			) {
				Debug_Fmt("** First slot in frame is not SELF or null symbol");
				Panic_Series(frame);
			}
		}

		if (IS_END(word) || IS_END(value)) {
			Debug_Fmt(
				"** Early %s end at index: %d",
				IS_END(word) ? "word" : "value",
				n
			);
			Panic_Series(frame);
		}

		if (!ANY_WORD(word)) {
			Debug_Fmt("** Non-word in word list, type: %d\n", VAL_TYPE(word));
			Panic_Series(words);
		}

		if (!VAL_GET_EXT(word, EXT_WORD_TYPED)) {
			Debug_Fmt("** Frame words contains non-'typed'-word");
			Panic_Series(words);
		}
	}

	if (NOT_END(word) || NOT_END(value)) {
		Debug_Fmt(
			"** Missing %s end at index: %d type: %d",
			NOT_END(word) ? "word" : "value",
			n,
			VAL_TYPE(word)
		);
		Panic_Series(frame);
	}
}
#endif
