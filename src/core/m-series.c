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
#include "sys-int-funcs.h"



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
*/	REBCNT Insert_Series(REBSER *series, REBCNT index, const REBYTE *data, REBCNT len)
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
	REBYTE wide = SERIES_WIDE(series);

	EXPAND_SERIES_TAIL(series, len);
	memcpy(series->data + (wide * tail), data, wide * len);
	CLEAR(series->data + (wide * series->tail), wide); // terminator
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
*/	REBSER *Copy_Sequence(REBSER *source)
/*
**		Copy any series that *isn't* an "array" (such as STRING!,
**		BINARY!, BITSET!, VECTOR!...).  Includes the terminator.
**
**		Use Copy_Array routines (which specify Shallow, Deep, etc.) for
**		greater detail needed when expressing intent for Rebol Arrays.
**
**		Note: No suitable name for "non-array-series" has been picked.
**		"Sequence" is used for now because Copy_Non_Array() doesn't
**		look good and lots of things aren't "Rebol Arrays" that aren't
**		series.  The main idea was just to get rid of the generic
**		Copy_Series() routine, which doesn't call any attention
**		to the importance of stating one's intentions specifically
**		about semantics when copying an array.
**
***********************************************************************/
{
	REBCNT len = source->tail + 1;
	REBSER *series = Make_Series(len, SERIES_WIDE(source), MKS_NONE);

	assert(!Is_Array_Series(source));

	memcpy(series->data, source->data, len * SERIES_WIDE(source));
	series->tail = source->tail;
	return series;
}


/***********************************************************************
**
*/	REBSER *Copy_Sequence_At_Len(REBSER *source, REBCNT index, REBCNT len)
/*
**		Copy a subseries out of a series that is not an array.
**		Includes the terminator for it.
**
**		Use Copy_Array routines (which specify Shallow, Deep, etc.) for
**		greater detail needed when expressing intent for Rebol Arrays.
**
***********************************************************************/
{
	REBSER *series = Make_Series(len + 1, SERIES_WIDE(source), MKS_NONE);

	assert(!Is_Array_Series(source));

	memcpy(
		series->data,
		source->data + index * SERIES_WIDE(source),
		(len + 1) * SERIES_WIDE(source)
	);
	series->tail = len;
	return series;
}


/***********************************************************************
**
*/	REBSER *Copy_Sequence_At_Position(const REBVAL *position)
/*
**		Copy a non-array series from its value structure, using the
**		value's index as the location to start copying the data.
**
***********************************************************************/
{
	return Copy_Sequence_At_Len(
		VAL_SERIES(position), VAL_INDEX(position), VAL_LEN(position)
	);
}


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
			CLEAR(series->data, SERIES_WIDE(series)); // terminate
		} else {
			// Add bias to head:
			REBCNT bias = SERIES_BIAS(series);
			if (REB_U32_ADD_OF(bias, len, &bias))
				raise Error_0(RE_OVERFLOW);

			if (bias > 0xffff) { //bias is 16-bit, so a simple SERIES_ADD_BIAS could overflow it
				REBYTE *data = series->data;

				data += SERIES_WIDE(series) * len;
				series->data -= SERIES_WIDE(series) * SERIES_BIAS(series);
				SERIES_REST(series) += SERIES_BIAS(series);
				SERIES_SET_BIAS(series, 0);

				memmove(series->data, data, SERIES_USED(series));
			} else {
				SERIES_SET_BIAS(series, bias);
				SERIES_REST(series) -= len;
				series->data += SERIES_WIDE(series) * len;
				if ((start = SERIES_BIAS(series))) {
					// If more than half biased:
					if (start >= MAX_SERIES_BIAS || start > SERIES_REST(series))
						Reset_Bias(series);
				}
			}
		}
		return;
	}

	if (index >= series->tail) return;

	start = index * SERIES_WIDE(series);

	// Clip if past end and optimize the remove operation:
	if (len + index >= series->tail) {
		series->tail = index;
		CLEAR(series->data + start, SERIES_WIDE(series));
		return;
	}

	length = (SERIES_LEN(series) + 1) * SERIES_WIDE(series); // include term.
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
	CLEAR(series->data + SERIES_WIDE(series) * series->tail, SERIES_WIDE(series));
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
	CLEAR(series->data, SERIES_WIDE(series)); // re-terminate
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
	CLEAR(series->data, SERIES_SPACE(series));
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
	CLEAR(series->data, SERIES_WIDE(series)); // re-terminate
}


/***********************************************************************
**
*/	void Terminate_Series(REBSER *series)
/*
**		Put terminator at tail of the series.
**
***********************************************************************/
{
	CLEAR(series->data + SERIES_WIDE(series) * series->tail, SERIES_WIDE(series));
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
	if (!buf) panic Error_0(RE_NO_BUFFER);

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

	ser = Make_Series(
		len + 1,
		SERIES_WIDE(buf),
		Is_Array_Series(buf) ? MKS_ARRAY : MKS_NONE
	);

	memcpy(ser->data, buf->data, SERIES_WIDE(buf) * len);
	ser->tail = len;
	TERM_SERIES(ser);

	return ser;
}


#if !defined(NDEBUG)

/***********************************************************************
**
*/	void Assert_Series_Term_Core(REBSER *series)
/*
***********************************************************************/
{
	if (Is_Array_Series(series)) {
		// REB_END values may not be canonized to zero bytes, check type only
		if (!IS_END(BLK_SKIP(series, series->tail))) {
			Debug_Fmt("Unterminated blocklike series detected");
			Panic_Series(series);
		}
	}
	else {
		// Non-REBVAL-bearing series must have their terminal as all 0 bytes
		int n;
		for (n = 0; n < SERIES_WIDE(series); n++) {
			if (0 != series->data[series->tail * SERIES_WIDE(series) + n]) {
				Debug_Fmt("Non-zero byte in terminator of non-block series");
				Panic_Series(series);
			}
		}
	}
}


/***********************************************************************
**
*/	void Panic_Series_Debug(const REBSER *series, const char *file, int line)
/*
**		This could be done in the PANIC_SERIES macro, but having it
**		as an actual function gives you a place to set breakpoints.
**
***********************************************************************/
{
	Debug_Fmt("Panic_Series() in %s at line %d", file, line);
	if (*series->guard == 1020) // should make valgrind or asan alert
		panic Error_0(RE_MISC);
	panic Error_0(RE_MISC); // just in case it didn't crash
}

#endif
