/***********************************************************************
**
**  REBOL Language Interpreter and Run-time Environment
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
**  Module:  m-series.c
**  Summary: implements REBOL's series concept
**  Section: memory
**  Author:  Carl Sassenrath
**
***********************************************************************/

#include "sys-core.h"


/***********************************************************************
**
*/	void Extend_Series(REBSER *series, REBCNT delta)
/*
**		Extend a series at its end without affecting its tail index.
**
***********************************************************************/
{
	REBCNT tail = series->tail;	// maintain tail position
	EXPAND_SERIES_TAIL(series, delta);
	series->tail = tail;
}


/***********************************************************************
**
*/	REBCNT Insert_Series(REBSER *series, REBCNT index, REBYTE *data, REBCNT len)
/*
**		Insert a series of values (bytes, longs, reb-vals) into the
**		series at the given index.  Expand it if necessary.  Does
**		not add a terminator to tail.
**
***********************************************************************/
{
	if (index > series->tail) index = series->tail;
	Expand_Series(series, index, len); // tail += len
	//Print("i: %d t: %d l: %d x: %d s: %d", index, series->tail, len, (series->tail + 1) * SERIES_WIDE(series), series->size);
	memcpy(series->data + (SERIES_WIDE(series) * index), data, SERIES_WIDE(series) * len);
	//*(int *)(series->data + (series->tail-1) * SERIES_WIDE(series)) = 5; // for debug purposes
	return index + len;
}


/***********************************************************************
**
*/	void Append_Series(REBSER *series, const REBYTE *data, REBCNT len)
/*
**		Append value(s) onto the tail of a series.  The len is
**		the number of units (bytes, REBVALS, etc.) of the data,
**		and does not include the terminator (which will be added).
**		The new tail position will be returned as the result.
**		A terminator will be added to the end of the appended data.
**
***********************************************************************/
{
	REBCNT tail = series->tail;
	REBCNT wide = SERIES_WIDE(series);

	EXPAND_SERIES_TAIL(series, len);
	memcpy(series->data + (wide * tail), data, wide * len);

	// !!! A value-based series REB_END is not canon; it only needs to
	// write the type byte.  Review cost of testing series width vs. writes

	memset(series->data + (wide * series->tail), NUL, wide); // terminator
}


/***********************************************************************
**
*/	void Append_Mem_Extra(REBSER *series, const REBYTE *data, REBCNT len, REBCNT extra)
/*
**		An optimized function for appending raw memory bytes to
**		a byte-sized series. The series will be expanded if room
**		is needed. A zero terminator will be added at the tail.
**		The extra size will be assured in the series, but is not
**		part of the appended length. (Allows adding additional bytes.)
**
***********************************************************************/
{
	REBCNT tail = series->tail;

	if ((tail + len + extra + 1) >= SERIES_REST(series)) {
		Expand_Series(series, tail, len+extra); // series->tail changed
		series->tail -= extra;
	}
	else {
		series->tail += len;
	}

	memcpy(series->data + tail, data, len);
	STR_TERM(series);
}


/***********************************************************************
**
*/	REBSER *Copy_Series(REBSER *source)
/*
**		Copy any series, including terminator for it.
**
***********************************************************************/
{
	REBCNT len = source->tail + 1;
	REBSER *series = Make_Series(len, SERIES_WIDE(source), FALSE);

	memcpy(series->data, source->data, len * SERIES_WIDE(source));
	series->tail = source->tail;
	return series;
}


/***********************************************************************
**
*/	REBSER *Copy_Series_Part(REBSER *source, REBCNT index, REBCNT length)
/*
**		Copy any subseries, including terminator for it.
**
***********************************************************************/
{
	REBSER *series = Make_Series(length+1, SERIES_WIDE(source), FALSE);

	memcpy(series->data, source->data + index * SERIES_WIDE(source), (length+1) * SERIES_WIDE(source));
	series->tail = length;
	return series;
}


/***********************************************************************
**
*/	REBSER *Copy_Series_Value(const REBVAL *value)
/*
**		Copy a series from its value structure.
**		Index does not need to be at head location.
**
***********************************************************************/
{
	return Copy_Series_Part(VAL_SERIES(value), VAL_INDEX(value), VAL_LEN(value));
}


#ifdef NOT_USED
/***********************************************************************
**
*/	REBINT Clone_Series(REBVAL *dst, REBVAL *src)
/*
**		Properly deep copy all types of series.
**		Return TRUE if BLOCK type.
**
***********************************************************************/
{
	Check_Stack();
	if (VAL_TYPE(src) < REB_BLOCK) {
		if (VAL_SERIES_WIDTH(src) == 4)
			VAL_SERIES(dst) = Make_Quad(VAL_BIN(src), VAL_TAIL(src));
		else
			VAL_SERIES(dst) = Copy_String(VAL_SERIES(src));
		return FALSE;
	} else {

		VAL_SERIES(dst) = Clone_Block(VAL_SERIES(src));
		if (IS_HASH(dst) || IS_LIST(dst))
			VAL_SERIES_SIDE(dst) = Copy_Side_Series(VAL_SERIES_SIDE(dst));
		return TRUE;
	}
}
#endif


/***********************************************************************
**
*/	void Remove_Series(REBSER *series, REBCNT index, REBINT len)
/*
**		Remove a series of values (bytes, longs, reb-vals) from the
**		series at the given index.
**
***********************************************************************/
{
	REBCNT	start;
	REBCNT	length;
	REBYTE	*data;

	if (len <= 0) return;

	// Optimized case of head removal:
	if (index == 0) {
		if ((REBCNT)len > series->tail) len = series->tail;
		SERIES_TAIL(series) -= len;
		if (SERIES_TAIL(series) == 0) {
			// Reset bias to zero:
			len = SERIES_BIAS(series);
			SERIES_SET_BIAS(series, 0);
			SERIES_REST(series) += len;
			series->data -= SERIES_WIDE(series) * len;

			// !!! Review in light of REB_END and non-canon...

			memset(series->data, NUL, SERIES_WIDE(series)); // terminate
		} else {
			// Add bias to head:
			SERIES_ADD_BIAS(series, len);
			SERIES_REST(series) -= len;
			series->data += SERIES_WIDE(series) * len;
			if (NZ(start = SERIES_BIAS(series))) {
				// If more than half biased:
				if (start >= MAX_SERIES_BIAS || start > SERIES_REST(series))
					Reset_Bias(series);
			}
		}
		return;
	}

	if (index >= series->tail) return;

	start = index * SERIES_WIDE(series);

	// Clip if past end and optimize the remove operation:
	if (len + index >= series->tail) {
		series->tail = index;
		memset(series->data + start, NUL, SERIES_WIDE(series));
		return;
	}

	length = SERIES_LEN(series) * SERIES_WIDE(series);
	series->tail -= (REBCNT)len;
	len *= SERIES_WIDE(series);
	data = series->data + start;
	memmove(data, data + len, length - (start + len));

	CHECK_MEMORY(5);
}


/***********************************************************************
**
*/	void Remove_Last(REBSER *series)
/*
**		Remove last value from a series.
**
***********************************************************************/
{
	if (series->tail == 0) return;
	series->tail--;

	// !!! REB_END is not canon, so you only have to write the type
	// byte... not zero everything.  Review.

	memset(
		series->data + SERIES_WIDE(series) * series->tail,
		NUL,
		SERIES_WIDE(series)
	);
}


/***********************************************************************
**
*/	void Reset_Bias(REBSER *series)
/*
**		Reset series bias.
**
***********************************************************************/
{
	REBCNT len;
	REBYTE *data = series->data;

	len = SERIES_BIAS(series);
	SERIES_SET_BIAS(series, 0);
	SERIES_REST(series) += len;
	series->data -= SERIES_WIDE(series) * len;

	memmove(series->data, data, SERIES_USED(series));
}


/***********************************************************************
**
*/	void Reset_Series(REBSER *series)
/*
**		Reset series to empty. Reset bias, tail, and termination.
**		The tail is reset to zero.
**
***********************************************************************/
{
	series->tail = 0;
	if (SERIES_BIAS(series)) Reset_Bias(series);

	// !!! REB_END is not canon, only type byte needs to be written

	memset(series->data, NUL, SERIES_WIDE(series)); // re-terminate
}


/***********************************************************************
**
*/	void Clear_Series(REBSER *series)
/*
**		Clear an entire series to zero. Resets bias and tail.
**		The tail is reset to zero.
**
***********************************************************************/
{
	series->tail = 0;
	if (SERIES_BIAS(series)) Reset_Bias(series);

	// !!! Review in light of REB_END only needing type bytes as zero

	memset(series->data, NUL, SERIES_SPACE(series));
}


/***********************************************************************
**
*/	void Resize_Series(REBSER *series, REBCNT size)
/*
**		Reset series and expand it to required size.
**		The tail is reset to zero.
**
***********************************************************************/
{
	series->tail = 0;
	if (SERIES_BIAS(series)) Reset_Bias(series);
	EXPAND_SERIES_TAIL(series, size);
	series->tail = 0;

	// !!! Review in light of REB_END only needing type bytes as zero

	memset(series->data, NUL, SERIES_WIDE(series)); // re-terminate
}


/***********************************************************************
**
*/	void Terminate_Series(REBSER *series)
/*
**		Put terminator at tail of the series.
**
***********************************************************************/
{
	// !!! Note that since REB_END values are not canonized, terminating
	// a value-based series only needs to write the byte indicating the
	// type...the other fields may remain uninitialized.

	memset(
		series->data + SERIES_WIDE(series) * series->tail,
		NUL,
		SERIES_WIDE(series)
	);
}


/***********************************************************************
**
*/  REBYTE *Reset_Buffer(REBSER *buf, REBCNT len)
/*
**		Setup to reuse a shared buffer. Expand it if needed.
**
**		NOTE:The tail is set to the length position.
**
***********************************************************************/
{
	if (!buf) Crash(RP_NO_BUFFER);

	RESET_TAIL(buf);
	if (SERIES_BIAS(buf)) Reset_Bias(buf);
	Expand_Series(buf, 0, len); // sets new tail

	return BIN_DATA(buf);
}


/***********************************************************************
**
*/  REBSER *Copy_Buffer(REBSER *buf, void *end)
/*
**		Copy a shared buffer. Set tail and termination.
**
***********************************************************************/
{
	REBSER *ser;
	REBCNT len;

	len = BYTE_SIZE(buf) ? ((REBYTE *)end) - BIN_HEAD(buf)
		: ((REBUNI *)end) - UNI_HEAD(buf);

	ser = Make_Series(len+1, SERIES_WIDE(buf), FALSE);

	memcpy(ser->data, buf->data, SERIES_WIDE(buf) * len);
	ser->tail = len;
	TERM_SERIES(ser);

	return ser;
}
