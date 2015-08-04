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
*/	REBSER *Make_Block(REBCNT length)
/*
**		Make a block series. Add 1 extra for the terminator.
**		Set TAIL to zero and set terminator.
**
***********************************************************************/
{
	REBSER *series = Make_Series(length + 1, sizeof(REBVAL), MKS_BLOCK);
	SET_END(BLK_HEAD(series));
	PG_Reb_Stats->Blocks++;
	return series;
}


/***********************************************************************
**
*/	REBSER *Copy_Block(REBSER *block, REBCNT index)
/*
**		Shallow copy a block from the given index thru the tail.
**
***********************************************************************/
{
	REBCNT len = SERIES_TAIL(block);
	REBSER *series;

	if (index > len) return Make_Block(0);

	len -= index;
	series = Make_Series(len + 1, sizeof(REBVAL), MKS_BLOCK);
	COPY_BLK_PART(series, BLK_SKIP(block, index), len);

	PG_Reb_Stats->Blocks++;

	return series;
}


/***********************************************************************
**
*/	REBSER *Copy_Block_Len(REBSER *block, REBCNT index, REBCNT len)
/*
**		Shallow copy a block from the given index for given length.
**
***********************************************************************/
{
	REBSER *series;

	if (index > SERIES_TAIL(block)) return Make_Block(0);
	if (index + len > SERIES_TAIL(block)) len = SERIES_TAIL(block) - index;

	series = Make_Series(len + 1, sizeof(REBVAL), MKS_BLOCK);
	COPY_BLK_PART(series, BLK_SKIP(block, index), len);

	PG_Reb_Stats->Blocks++;

	return series;
}


/***********************************************************************
**
*/	REBSER *Copy_Values(REBVAL *blk, REBCNT length)
/*
**		Shallow copy a block from current value for length values.
**
***********************************************************************/
{
	REBSER *series;

	series = Make_Series(length + 1, sizeof(REBVAL), MKS_BLOCK);
	COPY_BLK_PART(series, blk, length);

	PG_Reb_Stats->Blocks++;

	return series;
}


/***********************************************************************
**
*/	void Copy_Deep_Values(REBSER *block, REBCNT index, REBCNT tail, REBU64 types)
/*
**		Copy the contents of values specified by types. If the
**		DEEP flag is set, recurse into sub-blocks and objects.
**
***********************************************************************/
{
	REBVAL *val;

	for (; index < tail; index++) {

		val = BLK_SKIP(block, index);

		if ((types & TYPESET(VAL_TYPE(val)) & TS_SERIES_OBJ) != 0) {
			// Replace just the series field of the value
			// Note that this should work for objects too (the frame).
			VAL_SERIES(val) = Copy_Series(VAL_SERIES(val));
			if ((types & TYPESET(VAL_TYPE(val)) & TS_BLOCKS_OBJ) != 0) {
				PG_Reb_Stats->Blocks++;
				// If we need to copy recursively (deep):
				if ((types & CP_DEEP) != 0)
					Copy_Deep_Values(VAL_SERIES(val), 0, VAL_TAIL(val), types);
			}
		} else if (types & TYPESET(VAL_TYPE(val)) & TS_FUNCLOS)
			Clone_Function(val, val);
	}
}


/***********************************************************************
**
*/	REBSER *Copy_Block_Values(REBSER *block, REBCNT index, REBCNT tail, REBU64 types)
/*
**		Copy a block, copy specified values, deeply if indicated.
**
***********************************************************************/
{
	REBSER *series;

	if (index > tail) index = tail;
	if (index > SERIES_TAIL(block)) return Make_Block(0);

	series = Copy_Values(BLK_SKIP(block, index), tail - index);

	if (types != 0) Copy_Deep_Values(series, 0, SERIES_TAIL(series), types);

	return series;
}


/***********************************************************************
**
*/	REBSER *Clone_Block(REBSER *block)
/*
**		Deep copy block, including all series (strings and blocks),
**		but not images, bitsets, maps, etc.
**
***********************************************************************/
{
	return Copy_Block_Values(block, 0, SERIES_TAIL(block), TS_CODE);
}


/***********************************************************************
**
*/	REBSER *Clone_Block_Value(REBVAL *code)
/*
**		Same as above, but uses a value.
**
***********************************************************************/
{
	// Note: TAIL will be clipped to correct size if INDEX is not zero.
	return Copy_Block_Values(VAL_SERIES(code), VAL_INDEX(code), VAL_TAIL(code), TS_CODE);
}


/***********************************************************************
**
*/	REBSER *Copy_Expand_Block(REBSER *block, REBCNT extra)
/*
**		Create an expanded copy of the block, but with same tail.
**
***********************************************************************/
{
	REBCNT len = SERIES_TAIL(block);
	REBSER *series = Make_Series(len + extra + 1, sizeof(REBVAL), MKS_BLOCK);
	COPY_BLK_PART(series, BLK_HEAD(block), len);
	PG_Reb_Stats->Blocks++;
	return series;
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
	REBCNT type;

	if (into) {
		type = VAL_TYPE(into);
		series = VAL_SERIES(into);

		if (IS_PROTECT_SERIES(series)) Trap(RE_PROTECTED);

		if (ANY_BLOCK(into)) {
			// When the target is an any-block, we can do an ordinary
			// insertion of the values via a memcpy()-style operation

			len = Insert_Series(
				series, VAL_INDEX(into), cast(REBYTE*, blk), len
			);
		} else {
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

			// Len is used below to set the index of the result,
			// and we want it to be just past the last element
			// that we inserted
			len = VAL_INDEX(into);
		}
	} else {
		series = Make_Series(len + 1, sizeof(REBVAL), MKS_BLOCK);
		COPY_BLK_PART(series, blk, len);
		len = 0;
		type = REB_BLOCK;
		PG_Reb_Stats->Blocks++;
	}

	DSP = start;
	blk = DS_TOP;
	VAL_SET(blk, type);
	VAL_SERIES(blk) = series;
	VAL_INDEX(blk) = len;
}


/***********************************************************************
**
*/	REBVAL *Alloc_Tail_Blk(REBSER *block)
/*
**		Append a value to a block series at its tail.
**		Expand it if necessary. Update the termination and tail.
**		Returns the new value for you to initialize.
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
*/	void Append_Value(REBSER *block, const REBVAL *value)
/*
**		Append a value to a block series at its tail.
**		Expand it if necessary. Update the termination and tail.
**
***********************************************************************/
{
	*Alloc_Tail_Blk(block) = *value;
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

	for (bp = BLK_HEAD(blk); NOT_END(bp); bp++, index++) {

		if (VAL_TYPE(bp) >= REB_BLOCK &&
			VAL_TYPE(bp) <= REB_MAP &&
			VAL_BLK(bp) == VAL_BLK(val)
		) return index+1;

		if (
			VAL_TYPE(bp) >= REB_OBJECT &&
			VAL_TYPE(bp) <= REB_PORT &&
			VAL_OBJ_FRAME(bp) == VAL_OBJ_FRAME(val)
		) return index+1;
	}
	return -1;
}


/***********************************************************************
**
*/	void Clear_Value_Opts(REBSER *ser)
/*
**		Clear all options for values of a block series.
**
***********************************************************************/
{
	REBVAL *val = BLK_HEAD(ser);

	for (; NOT_END(val); val++) {
		VAL_OPTS(val) = 0;
	}
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
	// The next line works because VAL_OBJ_FRAME(val) == VAL_SERIES(val)
	REBSER *series = VAL_SERIES(val);
	if (!ANY_SERIES(val)
		&& !IS_OBJECT(val)
		&& !IS_MODULE(val)
		&& !IS_ERROR(val)
		&& !IS_PORT(val)) {
		return;
	}

	if (!SERIES_GET_FLAG(series, SER_MARK)) return; // avoid loop

	SERIES_CLR_FLAG(series, SER_MARK);

	for (val = VAL_BLK(val); NOT_END(val); val++)
		Unmark(val);
}
