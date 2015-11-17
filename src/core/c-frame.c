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

//
//  Check_Bind_Table: C
//
void Check_Bind_Table(void)
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

//
//  Make_Frame: C
// 
// Create a frame of a given size, allocating space for both
// words and values. Normally used for global frames.
//
REBSER *Make_Frame(REBINT len, REBOOL has_self)
{
	REBSER *frame;
	REBSER *words;
	REBVAL *value;

	words = Make_Array(len + 1); // size + room for SELF
	frame = Make_Series((len + 1) + 1, sizeof(REBVAL), MKS_ARRAY | MKS_FRAME);
	SET_END(BLK_HEAD(frame)); // !!! Needed since we used Make_Series?

	// Note: cannot use Append_Frame for first word.
	value = Alloc_Tail_Array(frame);
	SET_FRAME(value, 0, words);
	value = Alloc_Tail_Array(words);
	Val_Init_Typeset(value, ALL_64, has_self ? SYM_SELF : SYM_0);

	return frame;
}


//
//  Expand_Frame: C
// 
// Expand a frame. Copy words if flagged.
//
void Expand_Frame(REBSER *frame, REBCNT delta, REBCNT copy)
{
	REBSER *keylist = FRM_KEYLIST(frame);

	Extend_Series(frame, delta);
	TERM_ARRAY(frame);

	// Expand or copy WORDS block:
	if (copy) {
		REBOOL managed = SERIES_GET_FLAG(keylist, SER_MANAGED);
		FRM_KEYLIST(frame) = Copy_Array_Extra_Shallow(keylist, delta);
		if (managed) MANAGE_SERIES(FRM_KEYLIST(frame));
	}
	else {
		Extend_Series(keylist, delta);
		TERM_ARRAY(keylist);
	}
}


//
//  Append_Frame: C
// 
// Append a word to the frame word list. Expands the list
// if necessary. Returns the value cell for the word. (Set to
// UNSET by default to avoid GC corruption.)
// 
// If word is not NULL, use the word sym and bind the word value,
// otherwise use sym.
//
REBVAL *Append_Frame(REBSER *frame, REBVAL *word, REBCNT sym)
{
	REBSER *keylist = FRM_KEYLIST(frame);
	REBVAL *value;

	// Add to word list:
	EXPAND_SERIES_TAIL(keylist, 1);
	value = BLK_LAST(keylist);
	Val_Init_Typeset(value, ALL_64, word ? VAL_WORD_SYM(word) : sym);
	TERM_ARRAY(keylist);

	// Bind the word to this frame:
	if (word) {
		assert(sym == SYM_0);
		VAL_WORD_FRAME(word) = frame;
		VAL_WORD_INDEX(word) = frame->tail;
	}
	else
		assert(sym != SYM_0);

	// Add unset value to frame:
	EXPAND_SERIES_TAIL(frame, 1);
	value = BLK_LAST(frame);
	SET_UNSET(value);
	TERM_ARRAY(frame);

	return value; // The variable value location for the key we just added.
}


//
//  Collect_Keys_Start: C
// 
// Use the Bind_Table to start collecting new keys for a frame.
// Use Collect_Keys_End() when done.
// 
// WARNING: This routine uses the shared BUF_COLLECT rather than
// targeting a new series directly.  This way a frame can be
// allocated at exactly the right length when contents are copied.
// Therefore do not call code that might call BIND or otherwise
// make use of the Bind_Table or BUF_COLLECT.
//
void Collect_Keys_Start(REBCNT modes)
{
	REBINT *binds = WORDS_HEAD(Bind_Table); // GC safe to do here

	CHECK_BIND_TABLE;

	assert(SERIES_TAIL(BUF_COLLECT) == 0); // should be empty

	// Add the SELF key (or unused key) to slot zero
	if (modes & BIND_NO_SELF)
		Val_Init_Typeset(BLK_HEAD(BUF_COLLECT), ALL_64, SYM_0);
	else {
		Val_Init_Typeset(BLK_HEAD(BUF_COLLECT), ALL_64, SYM_SELF);
		binds[SYM_SELF] = -1;  // (cannot use zero here)
	}

	SERIES_TAIL(BUF_COLLECT) = 1;
}


//
//  Collect_Keys_End: C
// 
// Finish collecting words, and free the Bind_Table for reuse.
//
REBSER *Collect_Keys_End(REBSER *prior)
{
	REBVAL *words;
	REBINT *binds = WORDS_HEAD(Bind_Table); // GC safe to do here

	// Reset binding table (note BUF_COLLECT may have expanded):
	for (words = BLK_HEAD(BUF_COLLECT); NOT_END(words); words++)
		binds[VAL_TYPESET_CANON(words)] = 0;

	// If no new words, prior frame:
	if (prior && SERIES_TAIL(BUF_COLLECT) == SERIES_TAIL(prior)) {
		RESET_TAIL(BUF_COLLECT);  // allow reuse
		return FRM_KEYLIST(prior);
	}

	prior = Copy_Array_Shallow(BUF_COLLECT);
	RESET_TAIL(BUF_COLLECT);  // allow reuse

	CHECK_BIND_TABLE;

	return prior;
}


//
//  Collect_Object: C
// 
// Collect words from a prior object.
//
void Collect_Object(REBSER *prior)
{
	REBVAL *keys = FRM_KEYS(prior);
	REBINT *binds = WORDS_HEAD(Bind_Table);
	REBINT n;

	// this is necessary for memcpy below to not overwrite memory
	// BUF_COLLECT does not own
	RESIZE_SERIES(BUF_COLLECT, SERIES_TAIL(prior));

	// Typeset values in keys (with key symbol) can be copied just as bits
	assert(SERIES_TAIL(prior) > 0);
	if (IS_SELFLESS(prior)) {
		// A selfless frame can use its 0 slot for things other than words
		// (e.g. a CLOSURE! uses it for the function value of the closure
		// itself).  Eventually this will be what OBJECT! does, too...and
		// self will just be a way of establishing a relationship to that
		// 0 slot object value.
		memcpy(
			BLK_HEAD(BUF_COLLECT),
			keys + 1,
			(SERIES_TAIL(prior) - 1) * sizeof(REBVAL)
		);
	}
	else {
		memcpy(
			BLK_HEAD(BUF_COLLECT), keys, SERIES_TAIL(prior) * sizeof(REBVAL)
		);
	}

	SERIES_TAIL(BUF_COLLECT) = SERIES_TAIL(prior);
	for (n = 1, keys++; NOT_END(keys); keys++) // skips first = SELF
		binds[VAL_TYPESET_CANON(keys)] = n++;
}


//
//  Collect_Frame_Inner_Loop: C
// 
// The inner recursive loop used for Collect_Frame function below.
//
static void Collect_Frame_Inner_Loop(REBINT *binds, REBVAL value[], REBCNT modes)
{
	for (; NOT_END(value); value++) {
		if (ANY_WORD(value)) {
			if (!binds[VAL_WORD_CANON(value)]) {  // only once per word
				if (IS_SET_WORD(value) || modes & BIND_ALL) {
					REBVAL *typeset;
					binds[VAL_WORD_CANON(value)] = SERIES_TAIL(BUF_COLLECT);
					EXPAND_SERIES_TAIL(BUF_COLLECT, 1);
					typeset = BLK_LAST(BUF_COLLECT);
					Val_Init_Typeset(
						typeset,
						// Allow all datatypes but END or UNSET (initially):
						~((FLAGIT_64(REB_END) | FLAGIT_64(REB_UNSET))),
						VAL_WORD_SYM(value)
					);
				}
			} else {
				// If word duplicated:
				if (modes & BIND_NO_DUP) {
					// Reset binding table (note BUF_COLLECT may have expanded):
					REBVAL *key = BLK_HEAD(BUF_COLLECT);
					for (; NOT_END(key); key++)
						binds[VAL_TYPESET_CANON(key)] = 0;
					RESET_TAIL(BUF_COLLECT);  // allow reuse
					fail (Error(RE_DUP_VARS, value));
				}
			}
			continue;
		}
		// Recurse into sub-blocks:
		if (ANY_EVAL_BLOCK(value) && (modes & BIND_DEEP))
			Collect_Frame_Inner_Loop(binds, VAL_BLK_DATA(value), modes);
		// In this mode (foreach native), do not allow non-words:
		//else if (modes & BIND_GET) fail (Error_Invalid_Arg(value));
	}

	TERM_ARRAY(BUF_COLLECT);
}


//
//  Collect_Frame: C
// 
// Scans a block for words to use in the frame. The list of
// words can then be used to create a frame. The Bind_Table is
// used to quickly determine duplicate entries.
// 
// Returns:
//     A block of words that can be used for a frame word list.
//     If no new words, the prior list is returned.
// 
// Modes:
//     BIND_ALL  - scan all words, or just set words
//     BIND_DEEP - scan sub-blocks too
//     BIND_GET  - substitute :word with actual word
//     BIND_NO_SELF - do not add implicit SELF to the frame
//
REBSER *Collect_Frame(REBSER *prior, REBVAL value[], REBCNT modes)
{
	Collect_Keys_Start(modes);

	// Setup binding table with existing words:
	if (prior) Collect_Object(prior);

	// Scan for words, adding them to BUF_COLLECT and bind table:
	Collect_Frame_Inner_Loop(WORDS_HEAD(Bind_Table), &value[0], modes);

	return Collect_Keys_End(prior);
}


//
//  Collect_Words_Inner_Loop: C
// 
// Used for Collect_Words() after the binds table has
// been set up.
//
static void Collect_Words_Inner_Loop(REBINT *binds, REBVAL value[], REBCNT modes)
{
	for (; NOT_END(value); value++) {
		if (ANY_WORD(value)
			&& !binds[VAL_WORD_CANON(value)]
			&& (modes & BIND_ALL || IS_SET_WORD(value))
		) {
			REBVAL *word;
			binds[VAL_WORD_CANON(value)] = 1;
			word = Alloc_Tail_Array(BUF_COLLECT);
			Val_Init_Word_Unbound(word, REB_WORD, VAL_WORD_SYM(value));
		}
		else if (ANY_EVAL_BLOCK(value) && (modes & BIND_DEEP))
			Collect_Words_Inner_Loop(binds, VAL_BLK_DATA(value), modes);
	}
}


//
//  Collect_Words: C
// 
// Collect words from a prior block and new block.
//
REBSER *Collect_Words(REBVAL value[], REBVAL prior_value[], REBCNT modes)
{
	REBSER *series;
	REBCNT start;
	REBINT *binds = WORDS_HEAD(Bind_Table); // GC safe to do here
	CHECK_BIND_TABLE;

	assert(SERIES_TAIL(BUF_COLLECT) == 0); // should be empty

	if (prior_value)
		Collect_Words_Inner_Loop(binds, &prior_value[0], BIND_ALL);

	start = SERIES_TAIL(BUF_COLLECT);
	Collect_Words_Inner_Loop(binds, &value[0], modes);

	// Reset word markers:
	for (value = BLK_HEAD(BUF_COLLECT); NOT_END(value); value++)
		binds[VAL_WORD_CANON(value)] = 0;

	series = Copy_Array_At_Max_Shallow(
		BUF_COLLECT, start, SERIES_TAIL(BUF_COLLECT) - start
	);
	RESET_TAIL(BUF_COLLECT);  // allow reuse

	CHECK_BIND_TABLE;
	return series;
}


//
//  Create_Frame: C
// 
// Create a new frame from a word list.
// The values of the frame are initialized to NONE.
//
REBSER *Create_Frame(REBSER *words, REBSER *spec)
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


//
//  Rebind_Frame: C
// 
// Clone old src_frame to new dst_frame knowing
// which types of values need to be copied, deep copied, and rebound.
//
void Rebind_Frame(REBSER *src_frame, REBSER *dst_frame)
{
	// Rebind all values:
	Rebind_Block(src_frame, dst_frame, BLK_SKIP(dst_frame, 1), REBIND_FUNC);
}


//
//  Make_Object: C
// 
// Create an object from a parent object and a spec block.
// The words within the resultant object are not bound.
//
REBSER *Make_Object(REBSER *parent, REBVAL value[])
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
				Debug_Fmt(cs_cast(BOOT_STR(RS_WATCH, 2)), SERIES_TAIL(parent) - 1, FRM_KEYLIST(object));

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
			ENSURE_SERIES_MANAGED(FRM_KEYLIST(object));
			MANAGE_SERIES(object);
		}
		else {
			MANAGE_FRAME(object);
		}

		assert(words == FRM_KEYLIST(object));
	}

	ASSERT_SERIES_MANAGED(object);
	ASSERT_SERIES_MANAGED(FRM_KEYLIST(object));
	ASSERT_FRAME(object);
	return object;
}


//
//  Construct_Object: C
// 
// Construct an object (partial evaluation of block).
// Parent can be null. Values are rebound.
//
REBSER *Construct_Object(REBSER *parent, REBVAL value[], REBFLG as_is)
{
	REBSER *frame = Make_Object(parent, &value[0]);

	if (NOT_END(value)) Bind_Values_Shallow(&value[0], frame);

	if (as_is) Do_Min_Construct(&value[0]);
	else Do_Construct(&value[0]);

	return frame;
}


//
//  Make_Object_Block: C
// 
// Return a block containing words, values, or set-word: value
// pairs for the given object. Note: words are bound to original
// object.
// 
// Modes:
//     1 for word
//     2 for value
//     3 for words and values
//
REBSER *Make_Object_Block(REBSER *frame, REBINT mode)
{
	REBVAL *keys = FRM_KEYS(frame);
	REBVAL *values = FRM_VALUES(frame);
	REBSER *block;
	REBVAL *value;
	REBCNT n;

	n = (mode & 4) ? 0 : 1;
	block = Make_Array(SERIES_TAIL(frame) * (n + 1));

	for (; n < SERIES_TAIL(frame); n++) {
		if (!VAL_GET_EXT(keys + n, EXT_WORD_HIDE)) {
			if (mode & 1) {
				value = Alloc_Tail_Array(block);
				if (mode & 2) {
					VAL_SET(value, REB_SET_WORD);
					VAL_SET_OPT(value, OPT_VALUE_LINE);
				}
				else VAL_SET(value, REB_WORD);
				VAL_WORD_SYM(value) = VAL_TYPESET_SYM(keys + n);
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


//
//  Assert_Public_Object: C
//
void Assert_Public_Object(const REBVAL *value)
{
	REBVAL *key = BLK_HEAD(VAL_OBJ_KEYLIST(value));

	for (; NOT_END(key); key++)
		if (VAL_GET_EXT(key, EXT_WORD_HIDE)) fail (Error(RE_HIDDEN));
}


//
//  Make_Module: C
// 
// Create a module from a spec and an init block.
// Call the Make_Module function in the system/intrinsic object.
//
void Make_Module(REBVAL *out, const REBVAL *spec)
{
	if (Do_Sys_Func_Throws(out, SYS_CTX_MAKE_MODULE_P, spec, 0)) {
		// Gave back an unhandled RETURN, BREAK, CONTINUE, etc...
		fail (Error_No_Catch_For_Throw(out));
	}

	// !!! Shouldn't this be testing for !IS_MODULE(out)?
	if (IS_NONE(out)) fail (Error(RE_INVALID_SPEC, spec));
}


//
//  Make_Module_Spec: C
// 
// Create a module spec object. Holds module name, version,
// exports, locals, and more. See system/standard/module.
//
REBSER *Make_Module_Spec(REBVAL *spec)
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


//
//  Merge_Frames: C
// 
// Create a child frame from two parent frames. Merge common fields.
// Values from the second parent take precedence.
// 
// Deep copy and rebind the child.
//
REBSER *Merge_Frames(REBSER *parent1, REBSER *parent2)
{
	REBSER *wrds;
	REBSER *child;
	REBVAL *key;
	REBVAL *value;
	REBCNT n;
	REBINT *binds = WORDS_HEAD(Bind_Table);

	// Merge parent1 and parent2 words.
	// Keep the binding table.
	Collect_Keys_Start(BIND_ALL);
	// Setup binding table and BUF_COLLECT with parent1 words:
	Collect_Object(parent1);
	// Add parent2 words to binding table and BUF_COLLECT:
	Collect_Frame_Inner_Loop(
		binds, BLK_SKIP(FRM_KEYLIST(parent2), 1), BIND_ALL
	);

	// Allocate child (now that we know the correct size):
	wrds = Copy_Array_Shallow(BUF_COLLECT);
	child = Make_Array(SERIES_TAIL(wrds));
	value = Alloc_Tail_Array(child);
	VAL_SET(value, REB_FRAME);
	VAL_FRM_KEYLIST(value) = wrds;
	VAL_FRM_SPEC(value) = 0;

	// Copy parent1 values:
	memcpy(
		FRM_VALUES(child) + 1,
		FRM_VALUES(parent1) + 1,
		(SERIES_TAIL(parent1) - 1) * sizeof(REBVAL)
	);

	// Copy parent2 values:
	key = FRM_KEYS(parent2) + 1;
	value = FRM_VALUES(parent2) + 1;
	for (; NOT_END(key); key++, value++) {
		// no need to search when the binding table is available
		n = binds[VAL_TYPESET_CANON(key)];
		BLK_HEAD(child)[n] = *value;
	}

	// Terminate the child frame:
	SERIES_TAIL(child) = SERIES_TAIL(wrds);
	TERM_ARRAY(child);

	// Deep copy the child
	Clonify_Values_Len_Managed(
		BLK_SKIP(child, 1), SERIES_TAIL(child) - 1, TRUE, TS_CLONE
	);

	// Rebind the child
	Rebind_Block(parent1, child, BLK_SKIP(child, 1), REBIND_FUNC);
	Rebind_Block(parent2, child, BLK_SKIP(child, 1), REBIND_FUNC | REBIND_TABLE);

	// release the bind table
	Collect_Keys_End(child);

	return child;
}


//
//  Resolve_Context: C
// 
// Only_words can be a block of words or an index in the target
// (for new words).
//
void Resolve_Context(REBSER *target, REBSER *source, REBVAL *only_words, REBFLG all, REBFLG expand)
{
	REBINT *binds  = WORDS_HEAD(Bind_Table); // GC safe to do here
	REBVAL *keys;
	REBVAL *vals;
	REBINT n;
	REBINT m;
	REBCNT i = 0;

	CHECK_BIND_TABLE;

	if (IS_PROTECT_SERIES(target)) fail (Error(RE_PROTECTED));

	if (IS_INTEGER(only_words)) { // Must be: 0 < i <= tail
		i = VAL_INT32(only_words); // never <= 0
		if (i == 0) i = 1;
		if (i >= target->tail) return;
	}

	Collect_Keys_Start(BIND_NO_SELF);  // DO NOT TRAP IN THIS SECTION

	n = 0;

	// If limited resolve, tag the word ids that need to be copied:
	if (i) {
		// Only the new words of the target:
		for (keys = FRM_KEY(target, i); NOT_END(keys); keys++)
			binds[VAL_TYPESET_CANON(keys)] = -1;
		n = SERIES_TAIL(target) - 1;
	}
	else if (IS_BLOCK(only_words)) {
		// Limit exports to only these words:
		REBVAL *words = VAL_BLK_DATA(only_words);
		for (; NOT_END(words); words++) {
			if (IS_WORD(words) || IS_SET_WORD(words)) {
				binds[VAL_WORD_CANON(words)] = -1;
				n++;
			}
		}
	}

	// Expand target as needed:
	if (expand && n > 0) {
		// Determine how many new words to add:
		for (keys = FRM_KEY(target, 1); NOT_END(keys); keys++)
			if (binds[VAL_TYPESET_CANON(keys)]) n--;

		// Expand frame by the amount required:
		if (n > 0) Expand_Frame(target, n, 0);
		else expand = 0;
	}

	// Maps a word to its value index in the source context.
	// Done by marking all source words (in bind table):
	keys = FRM_KEYS(source) + 1;
	for (n = 1; NOT_END(keys); n++, keys++) {
		if (IS_UNSET(only_words) || binds[VAL_TYPESET_CANON(keys)])
			binds[VAL_TYPESET_CANON(keys)] = n;
	}

	// Foreach word in target, copy the correct value from source:
	n = i ? i : 1;
	vals = FRM_VALUE(target, n);
	keys = FRM_KEY(target, n);
	for (; NOT_END(keys); keys++, vals++) {
		if ((m = binds[VAL_TYPESET_CANON(keys)])) {
			binds[VAL_TYPESET_CANON(keys)] = 0; // mark it as set
			if (
				!VAL_GET_EXT(keys, EXT_WORD_LOCK)
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
		keys = FRM_KEYS(source) + 1;
		for (n = 1; NOT_END(keys); n++, keys++) {
			if (binds[VAL_TYPESET_CANON(keys)]) {
				// Note: no protect check is needed here
				binds[VAL_TYPESET_CANON(keys)] = 0;
				val = Append_Frame(target, 0, VAL_TYPESET_CANON(keys));
				*val = *FRM_VALUE(source, n);
			}
		}
	}
	else {
		// Reset bind table (do not use Collect_End):
		if (i) {
			for (keys = FRM_KEY(target, i); NOT_END(keys); keys++)
				binds[VAL_TYPESET_CANON(keys)] = 0;
		}
		else if (IS_BLOCK(only_words)) {
			REBVAL *words = VAL_BLK_DATA(only_words);
			for (; NOT_END(words); words++) {
				if (IS_WORD(words) || IS_SET_WORD(words))
					binds[VAL_WORD_CANON(words)] = 0;
			}
		}
		else {
			for (keys = FRM_KEYS(source) + 1; NOT_END(keys); keys++)
				binds[VAL_TYPESET_CANON(keys)] = 0;
		}
	}

	CHECK_BIND_TABLE;

	RESET_TAIL(BUF_COLLECT);  // allow reuse, trapping ok now
}


//
//  Bind_Values_Inner_Loop: C
// 
// Bind_Values_Core() sets up the binding table and then calls
// this recursive routine to do the actual binding.
//
static void Bind_Values_Inner_Loop(REBINT *binds, REBVAL value[], REBSER *frame, REBCNT mode)
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
		else if (ANY_ARRAY(value) && (mode & BIND_DEEP))
			Bind_Values_Inner_Loop(
				binds, VAL_BLK_DATA(value), frame, mode
			);
		else if ((IS_FUNCTION(value) || IS_CLOSURE(value)) && (mode & BIND_FUNC))
			Bind_Values_Inner_Loop(
				binds, BLK_HEAD(VAL_FUNC_BODY(value)), frame, mode
			);
	}
}


//
//  Bind_Values_Core: C
// 
// Bind words in an array of values terminated with REB_END
// to a specified frame.  See warnings on the functions like
// Bind_Values_Deep() about not passing just a singular REBVAL.
// 
// Different modes may be applied:
// 
//     BIND_ONLY - Only bind words found in the frame.
//     BIND_ALL  - Add words to the frame during the bind.
//     BIND_SET  - Add set-words to the frame during the bind.
//                 (note: word must not occur before the SET)
//     BIND_DEEP - Recurse into sub-blocks.
// 
// NOTE: BIND_SET must be used carefully, because it does not
// bind prior instances of the word before the set-word. That is
// to say that forward references are not allowed.
//
void Bind_Values_Core(REBVAL value[], REBSER *frame, REBCNT mode)
{
	REBVAL *key;
	REBCNT index;
	REBINT *binds = WORDS_HEAD(Bind_Table); // GC safe to do here

	CHECK_MEMORY(4);

	CHECK_BIND_TABLE;

	// Note about optimization: it's not a big win to avoid the
	// binding table for short blocks (size < 4), because testing
	// every block for the rare case adds up.

	// Setup binding table
	for (index = 1; index < frame->tail; index++) {
		key = FRM_KEY(frame, index);
		if (!VAL_GET_OPT(key, EXT_WORD_HIDE))
			binds[VAL_TYPESET_CANON(key)] = index;
	}

	Bind_Values_Inner_Loop(binds, &value[0], frame, mode);

	// Reset binding table:
	for (key = FRM_KEYS(frame) + 1; NOT_END(key); key++)
		binds[VAL_TYPESET_CANON(key)] = 0;

	CHECK_BIND_TABLE;
}


//
//  Unbind_Values_Core: C
// 
// Unbind words in a block, optionally unbinding those which are
// bound to a particular frame (if frame is NULL, then all
// words will be unbound regardless of their VAL_WORD_FRAME).
//
void Unbind_Values_Core(REBVAL value[], REBSER *frame, REBOOL deep)
{
	for (; NOT_END(value); value++) {
		if (ANY_WORD(value) && (!frame || VAL_WORD_FRAME(value) == frame))
			UNBIND_WORD(value);

		if (ANY_ARRAY(value) && deep)
			Unbind_Values_Core(VAL_BLK_DATA(value), frame, TRUE);
	}
}


//
//  Bind_Word: C
// 
// Binds a word to a frame. If word is not part of the
// frame, ignore it.
//
REBCNT Bind_Word(REBSER *frame, REBVAL *word)
{
	REBCNT n;

	n = Find_Word_Index(frame, VAL_WORD_SYM(word), FALSE);
	if (n != 0) {
		VAL_WORD_FRAME(word) = frame;
		VAL_WORD_INDEX(word) = n;
	}
	return n;
}


//
//  Bind_Relative_Inner_Loop: C
// 
// Recursive function for relative function word binding.
// 
// Note: frame arg points to an identifying series of the function,
// not a normal frame. This will be used to verify the word fetch.
//
static void Bind_Relative_Inner_Loop(REBINT *binds, REBSER *frame, REBSER *block)
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
		else if (ANY_ARRAY(value))
			Bind_Relative_Inner_Loop(binds, frame, VAL_SERIES(value));
	}
}


//
//  Bind_Relative: C
// 
// Bind the words of a function block to a stack frame.
// To indicate the relative nature of the index, it is set to
// a negative offset.
// 
// paramlist: VAL_FUNC_PARAMLIST(func)
// frame: VAL_FUNC_PARAMLIST(func)
// block: block to bind
//
void Bind_Relative(REBSER *paramlist, REBSER *frame, REBSER *block)
{
	REBVAL *params;
	REBINT index;
	REBINT *binds = WORDS_HEAD(Bind_Table); // GC safe to do here

	params = BLK_SKIP(paramlist, 1);

	CHECK_BIND_TABLE;

	//Dump_Block(words);

	// Setup binding table from the argument word list:
	for (index = 1; NOT_END(params); params++, index++)
		binds[VAL_TYPESET_CANON(params)] = -index;

	Bind_Relative_Inner_Loop(binds, frame, block);

	// Reset binding table:
	for (params = BLK_SKIP(paramlist, 1); NOT_END(params); params++)
		binds[VAL_TYPESET_CANON(params)] = 0;

	CHECK_BIND_TABLE;
}


//
//  Bind_Stack_Block: C
//
void Bind_Stack_Block(REBSER *frame, REBSER *block)
{
	Bind_Relative(frame, frame, block);
}


//
//  Bind_Stack_Word: C
//
void Bind_Stack_Word(REBSER *frame, REBVAL *word)
{
	REBINT index;

	index = Find_Param_Index(frame, VAL_WORD_SYM(word));
	if (!index) fail (Error(RE_NOT_IN_CONTEXT, word));
	VAL_WORD_FRAME(word) = frame;
	VAL_WORD_INDEX(word) = -index;
}


//
//  Rebind_Block: C
// 
// Rebind all words that reference src frame to dst frame.
// Rebind is always deep.
// 
// There are two types of frames: relative frames and normal frames.
// When frame_src type and frame_dst type differ,
// modes must have REBIND_TYPE.
//
void Rebind_Block(REBSER *src_frame, REBSER *dst_frame, REBVAL *data, REBFLG modes)
{
	REBINT *binds = WORDS_HEAD(Bind_Table);

	for (; NOT_END(data); data++) {
		if (ANY_ARRAY(data))
			Rebind_Block(src_frame, dst_frame, VAL_BLK_DATA(data), modes);
		else if (ANY_WORD(data) && VAL_WORD_FRAME(data) == src_frame) {
			VAL_WORD_FRAME(data) = dst_frame;
			if (modes & REBIND_TABLE) VAL_WORD_INDEX(data) = binds[VAL_WORD_CANON(data)];
			if (modes & REBIND_TYPE) VAL_WORD_INDEX(data) = - VAL_WORD_INDEX(data);
		} else if ((modes & REBIND_FUNC) && (IS_FUNCTION(data) || IS_CLOSURE(data)))
			Rebind_Block(src_frame, dst_frame, BLK_HEAD(VAL_FUNC_BODY(data)), modes);
	}
}


//
//  Find_Param_Index: C
// 
// Find function param word in function "frame".
//
REBCNT Find_Param_Index(REBSER *paramlist, REBCNT sym)
{
	REBVAL *params = BLK_SKIP(paramlist, 1);
	REBCNT len = SERIES_TAIL(paramlist);

	REBCNT canon = SYMBOL_TO_CANON(sym); // don't recalculate each time

	REBCNT n;
	for (n = 1; n < len; n++, params++) {
		if (
			sym == VAL_TYPESET_SYM(params)
			|| canon == VAL_TYPESET_CANON(params)
		) {
			return n;
		}
	}

	return 0;
}


//
//  Find_Word_Index: C
// 
// Search a frame looking for the given word symbol.
// Return the frame index for a word. Locate it by matching
// the canon word identifiers. Return 0 if not found.
//
REBCNT Find_Word_Index(REBSER *frame, REBCNT sym, REBFLG always)
{
	REBVAL *key = FRM_KEYS(frame) + 1;
	REBCNT len = SERIES_TAIL(FRM_KEYLIST(frame));

	REBCNT canon = SYMBOL_TO_CANON(sym); // always compare to CANON sym

	REBCNT n;
	for (n = 1; n < len; n++, key++) {
		if (
			sym == VAL_TYPESET_SYM(key)
			|| canon == VAL_TYPESET_CANON(key)
		) {
			return (!always && VAL_GET_EXT(key, EXT_WORD_HIDE)) ? 0 : n;
		}
	}

	return 0;
}


//
//  Find_Word_Value: C
// 
// Search a frame looking for the given word symbol and
// return the value for the word. Locate it by matching
// the canon word identifiers. Return NULL if not found.
//
REBVAL *Find_Word_Value(REBSER *frame, REBCNT sym)
{
	REBINT n;

	if (!frame) return 0;
	n = Find_Word_Index(frame, sym, FALSE);
	if (n == 0) return 0;
	return BLK_SKIP(frame, n);
}


//
//  Find_Word: C
// 
// Find word (of any type) in a block... quickly.
//
REBCNT Find_Word(REBSER *series, REBCNT index, REBCNT sym)
{
	REBVAL *value;

	for (; index < SERIES_TAIL(series); index++) {
		value = BLK_SKIP(series, index);
		if (ANY_WORD(value) && sym == VAL_WORD_CANON(value))
			return index;
	}

	return NOT_FOUND;
}


//
//  Get_Var_Core: C
// 
// Get the word--variable--value. (Generally, use the macros like
// GET_VAR or GET_MUTABLE_VAR instead of this).  This routine is
// called quite a lot and so attention to performance is important.
// 
// Coded assuming most common case is trap=TRUE and writable=FALSE
//
REBVAL *Get_Var_Core(const REBVAL *word, REBOOL trap, REBOOL writable)
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

			assert(
				SAME_SYM(
					VAL_WORD_SYM(word),
					VAL_TYPESET_SYM(FRM_KEYS(context) + index)
				)
			);

			if (
				writable &&
				VAL_GET_EXT(FRM_KEYS(context) + index, EXT_WORD_LOCK)
			) {
				if (trap) fail (Error(RE_LOCKED_WORD, word));
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
					&& context == VAL_FUNC_PARAMLIST(DSF_FUNC(call))
				) {
					REBVAL *value;

					assert(!IS_CLOSURE(DSF_FUNC(call)));

					assert(
						SAME_SYM(
							VAL_WORD_SYM(word),
							VAL_TYPESET_SYM(
								VAL_FUNC_PARAM(DSF_FUNC(call), -index)
							)
						)
					);

					if (
						writable &&
						VAL_GET_EXT(
							VAL_FUNC_PARAM(DSF_FUNC(call), -index),
							EXT_WORD_LOCK
						)
					) {
						if (trap) fail (Error(RE_LOCKED_WORD, word));
						return NULL;
					}

					value = DSF_ARG(call, -index);
					assert(!THROWN(value));
					return value;
				}

				call = PRIOR_DSF(call);
			}

			if (trap) fail (Error(RE_NO_RELATIVE, word));
			return NULL;
		}

		// ZERO INDEX: The word is SELF.  Although the information needed
		// to produce an OBJECT!-style REBVAL lives in the zero offset
		// of the frame, it's not a value that we can return a direct
		// pointer to.  Use GET_VAR_INTO instead for that.

		assert(!IS_SELFLESS(context));
		if (trap) fail (Error(RE_SELF_PROTECTED));
		return NULL; // is this a case where we should *always* trap?
	}

	if (trap) fail (Error(RE_NOT_BOUND, word));
	return NULL;
}


//
//  Get_Var_Into_Core: C
// 
// Variant of Get_Var_Core that always traps and never returns a
// direct pointer into a frame.  It is thus able to give back
// `self` lookups, and doesn't have to check the word's protection
// status before returning.
// 
// See comments in Get_Var_Core for what it's actually doing.
//
void Get_Var_Into_Core(REBVAL *out, const REBVAL *word)
{
	REBSER *context = VAL_WORD_FRAME(word);

	if (context) {
		REBINT index = VAL_WORD_INDEX(word);

		if (index > 0) {
			assert(
				SAME_SYM(
					VAL_WORD_SYM(word),
					VAL_TYPESET_SYM(FRM_KEYS(context) + index)
				)
			);

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
					&& context == VAL_FUNC_PARAMLIST(DSF_FUNC(call))
				) {
					assert(
						SAME_SYM(
							VAL_WORD_SYM(word),
							VAL_TYPESET_SYM(
								VAL_FUNC_PARAM(DSF_FUNC(call), -index)
							)
						)
					);
					assert(!IS_CLOSURE(DSF_FUNC(call)));
					*out = *DSF_ARG(call, -index);
					assert(!IS_TRASH(out));
					assert(!THROWN(out));
					return;
				}
				call = PRIOR_DSF(call);
			}

			fail (Error(RE_NO_RELATIVE, word));
		}

		// Key difference between Get_Var_Into and Get_Var...fabricating
		// an object REBVAL.

		// !!! Could fake function frames stow the function value itself
		// so 'binding-of' can return it and use for binding (vs. TRUE)?

		assert(!IS_SELFLESS(context));
		Val_Init_Object(out, context);
		return;
	}

	fail (Error(RE_NOT_BOUND, word));
}


//
//  Set_Var: C
// 
// Set the word (variable) value. (Use macro when possible).
//
void Set_Var(const REBVAL *word, const REBVAL *value)
{
	REBINT index = VAL_WORD_INDEX(word);
	struct Reb_Call *call;
	REBSER *frm;

	assert(!THROWN(value));

	if (!HAS_FRAME(word)) fail (Error(RE_NOT_BOUND, word));

	assert(VAL_WORD_FRAME(word));
//  Print("Set %s to %s [frame: %x idx: %d]", Get_Word_Name(word), Get_Type_Name(value), VAL_WORD_FRAME(word), VAL_WORD_INDEX(word));

	if (index > 0) {
		frm = VAL_WORD_FRAME(word);

		assert(
			SAME_SYM(
				VAL_WORD_SYM(word),
				VAL_TYPESET_SYM(FRM_KEYS(frm) + index)
			)
		);

		if (VAL_GET_EXT(FRM_KEYS(frm) + index, EXT_WORD_LOCK))
			fail (Error(RE_LOCKED_WORD, word));
		FRM_VALUES(frm)[index] = *value;
		return;
	}
	if (index == 0) fail (Error(RE_SELF_PROTECTED));

	// Find relative value:
	call = DSF;
	while (VAL_WORD_FRAME(word) != VAL_FUNC_PARAMLIST(DSF_FUNC(call))) {
		call = PRIOR_DSF(call);
		if (!call) fail (Error(RE_NO_RELATIVE, word));
	}

	assert(
		SAME_SYM(
			VAL_WORD_SYM(word),
			VAL_TYPESET_SYM(VAL_FUNC_PARAM(DSF_FUNC(call), -index))
		)
	);

	*DSF_ARG(call, -index) = *value;
}


//
//  Obj_Word: C
// 
// Return pointer to the nth WORD of an object.
//
REBVAL *Obj_Word(const REBVAL *value, REBCNT index)
{
	REBSER *obj = VAL_OBJ_KEYLIST(value);
	return BLK_SKIP(obj, index);
}


//
//  Obj_Value: C
// 
// Return pointer to the nth VALUE of an object.
// Return zero if the index is not valid.
//
REBVAL *Obj_Value(REBVAL *value, REBCNT index)
{
	REBSER *obj = VAL_OBJ_FRAME(value);

	if (index >= SERIES_TAIL(obj)) return 0;
	return BLK_SKIP(obj, index);
}


//
//  Init_Obj_Value: C
//
void Init_Obj_Value(REBVAL *value, REBSER *frame)
{
	assert(frame);
	CLEARS(value);
	Val_Init_Object(value, frame);
}


//
//  Init_Frame: C
//
void Init_Frame(void)
{
	// Temporary block used while scanning for frame words:
	Set_Root_Series(TASK_BUF_COLLECT, Make_Array(100), "word cache"); // just holds words, no GC
}


#ifndef NDEBUG
//
//  Assert_Frame_Core: C
//
void Assert_Frame_Core(REBSER *frame)
{
	REBINT n;
	REBVAL *value;
	REBVAL *key;
	REBINT tail;
	REBVAL *frame_value; // "FRAME!-typed value" at head of "frame" series

	frame_value = BLK_HEAD(frame);
	if (!IS_FRAME(frame_value)) Panic_Series(frame);

	if ((frame == VAL_SERIES(ROOT_ROOT)) || (frame == Task_Series)) {
		// !!! Currently it is allowed that the root frames not
		// have a wordlist.  This distinct behavior accomodation is
		// not worth having the variance of behavior, but since
		// it's there for now... allow it for just those two.

		if(!FRM_KEYLIST(frame))
			return;
	}

	value = FRM_VALUES(frame);

	key = FRM_KEYS(frame);
	tail = SERIES_TAIL(frame);

	for (n = 0; n < tail; n++, value++, key++) {
		if (n == 0) {
			if (
				!(IS_TYPESET(key) && (
					VAL_TYPESET_SYM(key) == SYM_SELF
					|| VAL_TYPESET_SYM(key) == SYM_0
				))
				&& !IS_CLOSURE(key)
			) {
				Debug_Fmt("** First frame slot not SELF, SYM_0 or CLOSURE!");
				Panic_Series(frame);
			}
		}
		else {
			if (!IS_TYPESET(key)) {
				Debug_Fmt("** Non-typeset in frame keys: %d\n", VAL_TYPE(key));
				Panic_Series(FRM_KEYLIST(frame));
			}
		}

		if (IS_END(key) || IS_END(value)) {
			Debug_Fmt(
				"** Early %s end at index: %d",
				IS_END(key) ? "key" : "value",
				n
			);
			Panic_Series(frame);
		}
	}

	if (NOT_END(key) || NOT_END(value)) {
		Debug_Fmt(
			"** Missing %s end at index: %d type: %d",
			NOT_END(key) ? "key" : "value",
			n,
			NOT_END(key) ? VAL_TYPE(key) : VAL_TYPE(value)
		);
		Panic_Series(frame);
	}
}
#endif
