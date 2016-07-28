//
//  File: %m-series.c
//  Summary: "implements REBOL's series concept"
//  Section: memory
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2016 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//

#include "sys-core.h"
#include "mem-series.h" // low-level series memory access
#include "sys-int-funcs.h"



//
//  Extend_Series: C
// 
// Extend a series at its end without affecting its tail index.
//
void Extend_Series(REBSER *s, REBCNT delta)
{
    REBCNT len_old = SER_LEN(s);
    EXPAND_SERIES_TAIL(s, delta);
    SET_SERIES_LEN(s, len_old);
}


//
//  Insert_Series: C
// 
// Insert a series of values (bytes, longs, reb-vals) into the
// series at the given index.  Expand it if necessary.  Does
// not add a terminator to tail.
//
REBCNT Insert_Series(
    REBSER *s,
    REBCNT index,
    const REBYTE *data,
    REBCNT len
) {
    if (index > SER_LEN(s))
        index = SER_LEN(s);

    Expand_Series(s, index, len); // tail += len

    memcpy(
        SER_DATA_RAW(s) + (SER_WIDE(s) * index),
        data,
        SER_WIDE(s) * len
    );

    return index + len;
}


//
//  Append_Series: C
// 
// Append value(s) onto the tail of a series.  The len is
// the number of units (bytes, REBVALS, etc.) of the data,
// and does not include the terminator (which will be added).
// The new tail position will be returned as the result.
// A terminator will be added to the end of the appended data.
//
void Append_Series(REBSER *s, const REBYTE *data, REBCNT len)
{
    REBCNT len_old = SER_LEN(s);
    REBYTE wide = SER_WIDE(s);

    assert(!Is_Array_Series(s));

    EXPAND_SERIES_TAIL(s, len);
    memcpy(SER_DATA_RAW(s) + (wide * len_old), data, wide * len);

    TERM_SERIES(s);
}


//
//  Append_Values_Len: C
// 
// Append value(s) onto the tail of an array.  The len is
// the number of units and does not include the terminator
// (which will be added).
//
void Append_Values_Len(REBARR *array, const REBVAL *head, REBCNT len)
{
    REBYTE *dest = cast(REBYTE*, ARR_TAIL(array));

    // updates tail (hence we calculated dest before)
    //
    EXPAND_SERIES_TAIL(ARR_SERIES(array), len);

    memcpy(dest, head, sizeof(REBVAL) * len);

    TERM_ARRAY_LEN(array, ARR_LEN(array));
}


//
//  Copy_Sequence: C
// 
// Copy any series that *isn't* an "array" (such as STRING!,
// BINARY!, BITSET!, VECTOR!...).  Includes the terminator.
// 
// Use Copy_Array routines (which specify Shallow, Deep, etc.) for
// greater detail needed when expressing intent for Rebol Arrays.
// 
// Note: No suitable name for "non-array-series" has been picked.
// "Sequence" is used for now because Copy_Non_Array() doesn't
// look good and lots of things aren't "Rebol Arrays" that aren't
// series.  The main idea was just to get rid of the generic
// Copy_Series() routine, which doesn't call any attention
// to the importance of stating one's intentions specifically
// about semantics when copying an array.
//
REBSER *Copy_Sequence(REBSER *original)
{
    REBCNT len = SER_LEN(original);
    REBSER *copy = Make_Series(len + 1, SER_WIDE(original), MKS_NONE);

    assert(!Is_Array_Series(original));

    memcpy(
        SER_DATA_RAW(copy),
        SER_DATA_RAW(original),
        len * SER_WIDE(original)
    );
    SET_SERIES_LEN(copy, SER_LEN(original));
    TERM_SERIES(copy);
    return copy;
}


//
//  Copy_Sequence_At_Len: C
// 
// Copy a subseries out of a series that is not an array.
// Includes the terminator for it.
// 
// Use Copy_Array routines (which specify Shallow, Deep, etc.) for
// greater detail needed when expressing intent for Rebol Arrays.
//
REBSER *Copy_Sequence_At_Len(REBSER *original, REBCNT index, REBCNT len)
{
    REBSER *copy = Make_Series(len + 1, SER_WIDE(original), MKS_NONE);

    assert(!Is_Array_Series(original));

    memcpy(
        SER_DATA_RAW(copy),
        SER_DATA_RAW(original) + index * SER_WIDE(original),
        (len + 1) * SER_WIDE(original)
    );
    SET_SERIES_LEN(copy, len);
    TERM_SEQUENCE(copy);
    return copy;
}


//
//  Copy_Sequence_At_Position: C
// 
// Copy a non-array series from its value structure, using the
// value's index as the location to start copying the data.
//
REBSER *Copy_Sequence_At_Position(const REBVAL *position)
{
    return Copy_Sequence_At_Len(
        VAL_SERIES(position), VAL_INDEX(position), VAL_LEN_AT(position)
    );
}


//
//  Remove_Series: C
// 
// Remove a series of values (bytes, longs, reb-vals) from the
// series at the given index.
//
void Remove_Series(REBSER *s, REBCNT index, REBINT len)
{
    if (len <= 0) return;

    REBOOL is_dynamic = GET_SER_FLAG(s, SERIES_FLAG_HAS_DYNAMIC);
    REBCNT len_old = SER_LEN(s);

    REBCNT start = index * SER_WIDE(s);

    // Optimized case of head removal.  For a dynamic series this may just
    // add "bias" to the head...rather than move any bytes.

    if (is_dynamic && index == 0) {
        if (cast(REBCNT, len) > len_old)
            len = len_old;

        s->content.dynamic.len -= len;
        if (s->content.dynamic.len == 0) {
            // Reset bias to zero:
            len = SER_BIAS(s);
            SER_SET_BIAS(s, 0);
            s->content.dynamic.rest += len;
            s->content.dynamic.data -= SER_WIDE(s) * len;
            TERM_SERIES(s);
        }
        else {
            // Add bias to head:
            REBCNT bias = SER_BIAS(s);
            if (REB_U32_ADD_OF(bias, len, &bias))
                fail (Error(RE_OVERFLOW));

            if (bias > 0xffff) { //bias is 16-bit, so a simple SER_ADD_BIAS could overflow it
                REBYTE *data = s->content.dynamic.data;

                data += SER_WIDE(s) * len;
                s->content.dynamic.data -= SER_WIDE(s) * SER_BIAS(s);

                s->content.dynamic.rest += SER_BIAS(s);
                SER_SET_BIAS(s, 0);

                memmove(
                    s->content.dynamic.data,
                    data,
                    SER_LEN(s) * SER_WIDE(s)
                );
                TERM_SERIES(s);
            }
            else {
                SER_SET_BIAS(s, bias);
                s->content.dynamic.rest -= len;
                s->content.dynamic.data += SER_WIDE(s) * len;
                if ((start = SER_BIAS(s)) != 0) {
                    // If more than half biased:
                    if (start >= MAX_SERIES_BIAS || start > SER_REST(s))
                        Unbias_Series(s, TRUE);
                }
            }
        }
        return;
    }

    if (index >= len_old) return;

    // Clip if past end and optimize the remove operation:

    if (len + index >= len_old) {
        SET_SERIES_LEN(s, index);
        TERM_SERIES(s);
        return;
    }

    // The terminator is not included in the length, because termination may
    // be implicit (e.g. there may not be a full SER_WIDE() worth of data
    // at the termination location).  Use TERM_SERIES() instead.
    //
    REBCNT length = SER_LEN(s) * SER_WIDE(s);
    SET_SERIES_LEN(s, len_old - cast(REBCNT, len));
    len *= SER_WIDE(s);

    REBYTE *data = SER_DATA_RAW(s) + start;
    memmove(data, data + len, length - (start + len));
    TERM_SERIES(s);
}


//
//  Unbias_Series: C
// 
// Reset series bias.
//
void Unbias_Series(REBSER *s, REBOOL keep)
{
    REBCNT len = SER_BIAS(s);
    if (len == 0)
        return;

    REBYTE *data = s->content.dynamic.data;

    SER_SET_BIAS(s, 0);
    s->content.dynamic.rest += len;
    s->content.dynamic.data -= SER_WIDE(s) * len;

    if (keep) {
        memmove(s->content.dynamic.data, data, SER_LEN(s) * SER_WIDE(s));
        TERM_SERIES(s);
    }
}


//
//  Reset_Series: C
// 
// Reset series to empty. Reset bias, tail, and termination.
// The tail is reset to zero.
//
void Reset_Series(REBSER *s)
{
    assert(!Is_Array_Series(s));
    if (GET_SER_FLAG(s, SERIES_FLAG_HAS_DYNAMIC)) {
        Unbias_Series(s, FALSE);
        s->content.dynamic.len = 0;
    }
    else
        SET_SERIES_LEN(s, 0);
    TERM_SERIES(s);
}


//
//  Reset_Array: C
// 
// Reset series to empty. Reset bias, tail, and termination.
// The tail is reset to zero.
//
void Reset_Array(REBARR *a)
{
    if (GET_ARR_FLAG(a, SERIES_FLAG_HAS_DYNAMIC))
        Unbias_Series(ARR_SERIES(a), FALSE);
    TERM_ARRAY_LEN(a, 0);
}


//
//  Clear_Series: C
// 
// Clear an entire series to zero. Resets bias and tail.
// The tail is reset to zero.
//
void Clear_Series(REBSER *s)
{
    assert(!GET_SER_FLAG(s, SERIES_FLAG_LOCKED));

    if (GET_SER_FLAG(s, SERIES_FLAG_HAS_DYNAMIC)) {
        Unbias_Series(s, FALSE);
        CLEAR(s->content.dynamic.data, SER_REST(s) * SER_WIDE(s));
    }
    else
        CLEAR(cast(REBYTE*, &s->content), sizeof(s->content));

    TERM_SERIES(s);
}


//
//  Resize_Series: C
// 
// Reset series and expand it to required size.
// The tail is reset to zero.
//
void Resize_Series(REBSER *s, REBCNT size)
{
    if (GET_SER_FLAG(s, SERIES_FLAG_HAS_DYNAMIC)) {
        s->content.dynamic.len = 0;
        Unbias_Series(s, TRUE);
    }
    else
        SET_SERIES_LEN(s, 0);

    EXPAND_SERIES_TAIL(s, size);
    SET_SERIES_LEN(s, 0);
    TERM_SERIES(s);
}


//
//  Reset_Buffer: C
// 
// Setup to reuse a shared buffer. Expand it if needed.
// 
// NOTE: The length will be set to the supplied value, but the series will
// not be terminated.
//
REBYTE *Reset_Buffer(REBSER *buf, REBCNT len)
{
    if (!buf) panic (Error(RE_NO_BUFFER));

    SET_SERIES_LEN(buf, 0);
    Unbias_Series(buf, TRUE);
    Expand_Series(buf, 0, len); // sets new tail

    return SER_DATA_RAW(buf);
}


//
//  Copy_Buffer: C
// 
// Copy a shared buffer, starting at index. Set tail and termination.
//
REBSER *Copy_Buffer(REBSER *buf, REBCNT index, void *end)
{
    assert(!Is_Array_Series(buf));

    REBCNT len = BYTE_SIZE(buf)
        ? cast(REBYTE*, end) - BIN_HEAD(buf)
        : cast(REBUNI*, end) - UNI_HEAD(buf);

    if (index) len -= index;

    REBSER *copy = Make_Series(len + 1, SER_WIDE(buf), MKS_NONE);

    memcpy(
        SER_DATA_RAW(copy),
        SER_DATA_RAW(buf) + index * SER_WIDE(buf),
        SER_WIDE(buf) * len
    );
    SET_SERIES_LEN(copy, len);
    TERM_SEQUENCE(copy);

    return copy;
}


#if !defined(NDEBUG)

//
//  Assert_Series_Term_Core: C
//
void Assert_Series_Term_Core(REBSER *series)
{
    if (Is_Array_Series(series)) {
        //
        // END values aren't canonized to zero bytes, check IS_END explicitly
        //
        RELVAL *tail = ARR_TAIL(AS_ARRAY(series));
        if (NOT_END(tail)) {
            printf("Unterminated blocklike series detected\n");
            fflush(stdout);
            Panic_Series(series);
        }
    }
    else {
        //
        // If they are terminated, then non-REBVAL-bearing series must have
        // their terminal element as all 0 bytes (to use this check)
        //
        REBCNT len = SER_LEN(series);
        REBCNT wide = SER_WIDE(series);
        REBCNT n;
        for (n = 0; n < wide; n++) {
            if (0 != SER_DATA_RAW(series)[(len * wide) + n]) {
                printf("Non-zero byte in terminator of non-block series\n");
                fflush(stdout);
                Panic_Series(series);
            }
        }
    }
}


//
//  Assert_Series_Core: C
//
void Assert_Series_Core(REBSER *series)
{
    if (IS_FREE_NODE(series))
        Panic_Series(series);

    assert(SER_LEN(series) < SER_REST(series));

    Assert_Series_Term_Core(series);
}


//
//  Panic_Series_Debug: C
// 
// This could be done in the PANIC_SERIES macro, but having it
// as an actual function gives you a place to set breakpoints.
//
ATTRIBUTE_NO_RETURN void Panic_Series_Debug(
    REBSER *series,
    const char *file,
    int line
) {
    // Note: Only use printf in debug builds (not a dependency of the main
    // executable).  Here it's important because series panics can happen
    // during mold and other times.
    //
    printf("\n\n*** Panic_Series() in %s at line %d\n", file, line);
    if (IS_FREE_NODE(series))
        printf("Likely freed ");
    else
        printf("Likely created ");
    printf("during evaluator tick: %d\n", cast(REBCNT, series->do_count));
    fflush(stdout);

    if (*series->guard == 1020) // should make valgrind or asan alert
        panic (Error(RE_MISC));

    printf("!!! *series->guard didn't trigger ASAN/Valgrind trap\n");
    printf("!!! either not a REBSER, or you're not running ASAN/Valgrind\n");
    fflush(stdout);

    panic (Error(RE_MISC)); // just in case it didn't crash
}

#endif
