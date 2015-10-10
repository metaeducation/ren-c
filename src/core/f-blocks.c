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
**  Module:  f-blocks.c
**  Summary: primary block series support functions
**  Section: functional
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

#include "sys-core.h"


/***********************************************************************
**
*/	REBSER *Make_Array(REBCNT capacity)
/*
**		Make a series that is the right size to store REBVALs (and
**		marked for the garbage collector to look into recursively).
**		Terminator included implicitly. Sets TAIL to zero.
**
***********************************************************************/
{
	REBSER *series = Make_Series(capacity + 1, sizeof(REBVAL), MKS_ARRAY);
	SET_END(BLK_HEAD(series));

	return series;
}


/***********************************************************************
**
*/	REBSER *Copy_Array_At_Extra_Shallow(REBSER *array, REBCNT index, REBCNT extra)
/*
**		Shallow copy an array from the given index thru the tail.
**		Additional capacity beyond what is required can be added
**		by giving an `extra` count of how many value cells one needs.
**
***********************************************************************/
{
	REBCNT len = SERIES_TAIL(array);
	REBSER *series;

	if (index > len) return Make_Array(extra);

	len -= index;
	series = Make_Series(len + extra + 1, sizeof(REBVAL), MKS_ARRAY);

	memcpy(series->data, BLK_SKIP(array, index), len * sizeof(REBVAL));
	SERIES_TAIL(series) = len;
	BLK_TERM(series);

	return series;
}


/***********************************************************************
**
*/	REBSER *Copy_Array_At_Max_Shallow(REBSER *array, REBCNT index, REBCNT max)
/*
**		Shallow copy an array from the given index for given maximum
**		length (clipping if it exceeds the array length)
**
***********************************************************************/
{
	REBSER *series;

	if (index > SERIES_TAIL(array)) return Make_Array(0);
	if (index + max > SERIES_TAIL(array)) max = SERIES_TAIL(array) - index;

	series = Make_Series(max + 1, sizeof(REBVAL), MKS_ARRAY);

	memcpy(series->data, BLK_SKIP(array, index), max * sizeof(REBVAL));
	SERIES_TAIL(series) = max;
	BLK_TERM(series);

	return series;
}


/***********************************************************************
**
*/	REBSER *Copy_Values_Len_Shallow(REBVAL value[], REBCNT len)
/*
**		Shallow copy the first 'len' values of `value[]` into a new
**		series created to hold exactly that many entries.
**
***********************************************************************/
{
	REBSER *series;

	series = Make_Series(len + 1, sizeof(REBVAL), MKS_ARRAY);

	memcpy(series->data, &value[0], len * sizeof(REBVAL));
	SERIES_TAIL(series) = len;
	BLK_TERM(series);

	return series;
}


/***********************************************************************
**
*/	void Clonify_Values_Len_Managed(REBVAL value[], REBCNT len, REBOOL deep, REBU64 types)
/*
**		Update the first `len` elements of value[] to clone the series
**		embedded in them *if* they are in the given set of types (and
**		if "cloning" makes sense for them, e.g. they are not simple
**		scalars).  If the `deep` flag is set, recurse into subseries
**		and objects when that type is matched for clonifying.
**
**		Note: The resulting clones will be managed.  The model for
**		lists only allows the topmost level to contain unmanaged
**		values...and we *assume* the values we are operating on here
**		live inside of an array.  (We also assume the source values
**		are in an array, and assert that they are managed.)
**
***********************************************************************/
{
	REBCNT index;

	for (index = 0; index < len; index++, value++) {
		// By the rules, if we need to do a deep copy on the source
		// series then the values inside it must have already been
		// marked managed (because they *might* delve another level deep)
		ASSERT_VALUE_MANAGED(value);

		if (types & FLAGIT_64(VAL_TYPE(value)) & TS_SERIES_OBJ) {
			REBSER *series;

			if (ANY_OBJECT(value)) {
				series = Copy_Array_Shallow(VAL_OBJ_FRAME(value));
				VAL_OBJ_FRAME(value) = series;
			}
			else {
				if (Is_Array_Series(VAL_SERIES(value)))
					series = Copy_Array_Shallow(VAL_SERIES(value));
				else
					series = Copy_Sequence(VAL_SERIES(value));
				VAL_SERIES(value) = series;
			}

			MANAGE_SERIES(series);

			if (!deep) continue;

			if (types & FLAGIT_64(VAL_TYPE(value)) & TS_ARRAYS_OBJ) {
				Clonify_Values_Len_Managed(
					 BLK_HEAD(series), VAL_TAIL(value), deep, types
				);
			}
		}
		else if (types & FLAGIT_64(VAL_TYPE(value)) & TS_FUNCLOS) {
			// Here we reuse the spec of the function when we copy it, but
			// create a new identifying word series.  We also need to make
			// a new body and rebind it to that series.  The reason we have
			// to copy the function is because it can persistently modify
			// its body (in the current design) so a copy would need to
			// capture that state.  Also, the word series is used to identify
			// function instances distinctly so two calls on the stack won't
			// be seen as recursions of the same function, sharing each others
			// "stack relative locals".

			// !!! Closures can probably be left as-is, since they always
			// copy their bodies and cannot accumulate state in their
			// archetype.  This would have to be tested further.
			//
			// if (IS_CLOSURE(value)) continue;

			REBSER *src_words = VAL_FUNC_PARAMLIST(value);

			VAL_FUNC_PARAMLIST(value) = Copy_Array_Shallow(src_words);
			MANAGE_SERIES(VAL_FUNC_PARAMLIST(value));

			VAL_FUNC_BODY(value) = Copy_Array_Core_Managed(
				VAL_FUNC_BODY(value),
				0,
				SERIES_TAIL(VAL_FUNC_BODY(value)),
				TRUE, // deep
				TS_CLONE
			);

			// Remap references in the body from src_words to our new copied
			// word list we saved in VAL_FUNC_PARAMLIST(value)
			Rebind_Block(
				src_words,
				VAL_FUNC_PARAMLIST(value),
				BLK_HEAD(VAL_FUNC_BODY(value)),
				0
			);
		}
		else {
			// The value is not on our radar as needing to be processed,
			// so leave it as-is.
		}
	}
}


/***********************************************************************
**
*/	REBSER *Copy_Array_Core_Managed(REBSER *block, REBCNT index, REBCNT tail, REBOOL deep, REBU64 types)
/*
**		Copy a block, copy specified values, deeply if indicated.
**
**		The resulting series will already be under GC management,
**		and hence cannot be freed with Free_Series().
**
***********************************************************************/
{
	REBSER *series;

	assert(Is_Array_Series(block));

	if (index > tail) index = tail;

	if (index > SERIES_TAIL(block)) {
		series = Make_Array(0);
		MANAGE_SERIES(series);
	}
	else {
		series = Copy_Values_Len_Shallow(BLK_SKIP(block, index), tail - index);
		MANAGE_SERIES(series);

		if (types != 0)
			Clonify_Values_Len_Managed(
				BLK_HEAD(series), SERIES_TAIL(series), deep, types
			);
	}

	return series;
}


/***********************************************************************
**
*/	REBSER *Copy_Array_At_Deep_Managed(REBSER *array, REBCNT index)
/*
**		Deep copy an array, including all series (strings, blocks,
**		parens, objects...) excluding images, bitsets, maps, etc.
**		The set of exclusions is the typeset TS_NOT_COPIED.
**
**		The resulting array will already be under GC management,
**		and hence cannot be freed with Free_Series().
**
**		Note: If this were declared as a macro it would use the
**		`array` parameter more than once, and have to be in all-caps
**		to warn against usage with arguments that have side-effects.
**
***********************************************************************/
{
	return Copy_Array_Core_Managed(
		array,
		index,
		SERIES_TAIL(array),
		TRUE, // deep
		TS_SERIES & ~TS_NOT_COPIED
	);
}


/***********************************************************************
**
*/	void Copy_Stack_Values(REBINT start, REBVAL *into)
/*
**		Copy computed values from the stack into the series
**		specified by "into", or if into is NULL then store it as a
**		block on top of the stack.  (Also checks to see if into
**		is protected, and will trigger a trap if that is the case.)
**
***********************************************************************/
{
	// REVIEW: Can we change the interface to not take a REBVAL
	// for into, in order to better show the subtypes allowed here?
	// Currently it can be any-block!, any-string!, or binary!

	REBSER *series;
	REBVAL *blk = DS_AT(start);
	REBCNT len = DSP - start + 1;

	if (into) {
		series = VAL_SERIES(into);

		if (IS_PROTECT_SERIES(series)) raise Error_0(RE_PROTECTED);

		if (ANY_BLOCK(into)) {
			// When the target is an any-block, we can do an ordinary
			// insertion of the values via a memcpy()-style operation

			VAL_INDEX(into) = Insert_Series(
				series, VAL_INDEX(into), cast(REBYTE*, blk), len
			);

			DS_DROP_TO(start);

			Val_Init_Series_Index(
				DS_TOP, VAL_TYPE(into), series, VAL_INDEX(into)
			);
		}
		else {
			// When the target is a string or binary series, we defer
			// to the same code used by A_INSERT.  Because the interface
			// does not take a memory address and count, we insert
			// the values one by one.

			// REVIEW: Is there a way to do this without the loop,
			// which may be able to make a better guess of how much
			// to expand the target series by based on the size of
			// the operation?

			REBCNT i;
			REBCNT flags = 0;
			// you get weird behavior if you don't do this
			if (IS_BINARY(into)) SET_FLAG(flags, AN_SERIES);
			for (i = 0; i < len; i++) {
				VAL_INDEX(into) += Modify_String(
					A_INSERT,
					VAL_SERIES(into),
					VAL_INDEX(into) + i,
					blk + i,
					flags,
					1, // insert one element at a time
					1 // duplication count
				);
			}

			DS_DROP_TO(start);

			// We want index of result just past the last element we inserted
			Val_Init_Series_Index(
				DS_TOP, VAL_TYPE(into), series, VAL_INDEX(into)
			);
		}
	}
	else {
		series = Make_Series(len + 1, sizeof(REBVAL), MKS_ARRAY);

		memcpy(series->data, blk, len * sizeof(REBVAL));
		SERIES_TAIL(series) = len;
		BLK_TERM(series);

		DS_DROP_TO(start);
		Val_Init_Series_Index(DS_TOP, REB_BLOCK, series, 0);
	}
}


/***********************************************************************
**
*/	REBVAL *Alloc_Tail_Array(REBSER *block)
/*
**		Append a REBVAL-size slot to Rebol Array series at its tail.
**		Will use existing memory capacity already in the series if it
**		is available, but will expand the series if necessary.
**		Returns the new value for you to initialize.
**
**		Note: Updates the termination and tail.
**
***********************************************************************/
{
	REBVAL *tail;

	EXPAND_SERIES_TAIL(block, 1);
	tail = BLK_TAIL(block);
	SET_END(tail);

	SET_TRASH(tail - 1); // No-op in release builds
	return tail - 1;
}


/***********************************************************************
**
*/	REBINT Find_Same_Block(REBSER *blk, const REBVAL *val)
/*
**		Scan a block for any values that reference blocks related
**		to the value provided.
**
**		Defect: only checks certain kinds of values.
**
***********************************************************************/
{
	REBVAL *bp;
	REBINT index = 0;

	REBSER *compare;

	if (VAL_TYPE(val) >= REB_BLOCK && VAL_TYPE(val) <= REB_MAP)
		compare = VAL_SERIES(val);
	else if (VAL_TYPE(val) >= REB_BLOCK && VAL_TYPE(val) <= REB_PORT)
		compare = VAL_OBJ_FRAME(val);
	else {
		assert(FALSE);
		DEAD_END;
	}

	for (bp = BLK_HEAD(blk); NOT_END(bp); bp++, index++) {

		if (VAL_TYPE(bp) >= REB_BLOCK &&
			VAL_TYPE(bp) <= REB_MAP &&
			VAL_SERIES(bp) == compare
		) return index+1;

		if (
			VAL_TYPE(bp) >= REB_OBJECT &&
			VAL_TYPE(bp) <= REB_PORT &&
			VAL_OBJ_FRAME(bp) == compare
		) return index+1;
	}
	return -1;
}



/***********************************************************************
**
*/	void Unmark(REBVAL *val)
/*
**		Clear the recusion markers for series and object trees.
**
**		Note: these markers are also used for GC. Functions that
**		call this must not be able to trigger GC!
**
***********************************************************************/
{
	REBSER *series;
	if (ANY_SERIES(val))
		series = VAL_SERIES(val);
	else if (IS_OBJECT(val) || IS_MODULE(val) || IS_ERROR(val) || IS_PORT(val))
		series = VAL_OBJ_FRAME(val);
	else
		return;

	if (!SERIES_GET_FLAG(series, SER_MARK)) return; // avoid loop

	SERIES_CLR_FLAG(series, SER_MARK);

	for (val = VAL_BLK_HEAD(val); NOT_END(val); val++)
		Unmark(val);
}
