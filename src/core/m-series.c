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
// Copyright 2012-2017 Rebol Open Source Contributors
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
void Append_Series(REBSER *s, const void *data, REBCNT len)
{
    REBCNT len_old = SER_LEN(s);
    REBYTE wide = SER_WIDE(s);

    assert(not IS_SER_ARRAY(s));

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
void Append_Values_Len(REBARR *a, const REBVAL *head, REBCNT len)
{
    REBCNT old_len = ARR_LEN(a);

    // updates tail, which could move data storage.
    //
    EXPAND_SERIES_TAIL(SER(a), len);

    // `char*` casts needed: https://stackoverflow.com/q/57721104
    memcpy(
        cast(char*, ARR_AT(a, old_len)),
        cast(char*, head),
        sizeof(REBVAL) * len
    );

    TERM_ARRAY_LEN(a, ARR_LEN(a));
}


//
//  Copy_Sequence_Core: C
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
REBSER *Copy_Sequence_Core(REBSER *s, REBFLGS flags)
{
    assert(not IS_SER_ARRAY(s));

    REBCNT len = SER_LEN(s);
    REBSER *copy = Make_Ser_Core(len + 1, SER_WIDE(s), flags);

    memcpy(SER_DATA_RAW(copy), SER_DATA_RAW(s), len * SER_WIDE(s));
    TERM_SEQUENCE_LEN(copy, SER_LEN(s));
    return copy;
}


//
//  Copy_Sequence_At_Len_Extra: C
//
// Copy a subseries out of a series that is not an array.
// Includes the terminator for it.
//
// Use Copy_Array routines (which specify Shallow, Deep, etc.) for
// greater detail needed when expressing intent for Rebol Arrays.
//
REBSER *Copy_Sequence_At_Len_Extra(
    REBSER *s,
    REBCNT index,
    REBCNT len,
    REBCNT extra
){
    assert(not IS_SER_ARRAY(s));

    REBSER *copy = Make_Ser(len + 1 + extra, SER_WIDE(s));
    memcpy(
        SER_DATA_RAW(copy),
        SER_DATA_RAW(s) + index * SER_WIDE(s),
        (len + 1) * SER_WIDE(s)
    );
    TERM_SEQUENCE_LEN(copy, len);
    return copy;
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

    bool is_dynamic = IS_SER_DYNAMIC(s);
    REBCNT len_old = SER_LEN(s);

    REBCNT start = index * SER_WIDE(s);

    // Optimized case of head removal.  For a dynamic series this may just
    // add "bias" to the head...rather than move any bytes.

    if (is_dynamic and index == 0) {
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
            unsigned int bias;
            if (REB_U32_ADD_OF(SER_BIAS(s), len, &bias))
                fail (Error_Overflow_Raw());

            if (bias > 0xffff) { // 16-bit, simple SER_ADD_BIAS could overflow
                REBYTE *data = cast(REBYTE*, s->content.dynamic.data);

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
                    if (start >= MAX_SERIES_BIAS or start > SER_REST(s))
                        Unbias_Series(s, true);
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
void Unbias_Series(REBSER *s, bool keep)
{
    REBCNT len = SER_BIAS(s);
    if (len == 0)
        return;

    REBYTE *data = cast(REBYTE*, s->content.dynamic.data);

    SER_SET_BIAS(s, 0);
    s->content.dynamic.rest += len;
    s->content.dynamic.data -= SER_WIDE(s) * len;

    if (keep) {
        memmove(s->content.dynamic.data, data, SER_LEN(s) * SER_WIDE(s));
        TERM_SERIES(s);
    }
}


//
//  Reset_Sequence: C
//
// Reset series to empty. Reset bias, tail, and termination.
// The tail is reset to zero.
//
void Reset_Sequence(REBSER *s)
{
    assert(not IS_SER_ARRAY(s));
    if (IS_SER_DYNAMIC(s)) {
        Unbias_Series(s, false);
        s->content.dynamic.len = 0;
        TERM_SEQUENCE(s);
    }
    else
        TERM_SEQUENCE_LEN(s, 0);
}


//
//  Reset_Array: C
//
// Reset series to empty. Reset bias, tail, and termination.
// The tail is reset to zero.
//
void Reset_Array(REBARR *a)
{
    if (IS_SER_DYNAMIC(a))
        Unbias_Series(SER(a), false);
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
    assert(!Is_Series_Read_Only(s));

    if (IS_SER_DYNAMIC(s)) {
        Unbias_Series(s, false);
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
    if (IS_SER_DYNAMIC(s)) {
        s->content.dynamic.len = 0;
        Unbias_Series(s, true);
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
    if (buf == NULL)
        panic ("buffer not yet allocated");

    SET_SERIES_LEN(buf, 0);
    Unbias_Series(buf, true);
    Expand_Series(buf, 0, len); // sets new tail

    return SER_DATA_RAW(buf);
}


#if !defined(NDEBUG)

//
//  Assert_Series_Term_Core: C
//
void Assert_Series_Term_Core(REBSER *s)
{
    if (IS_SER_ARRAY(s)) {
        //
        // END values aren't canonized to zero bytes, check IS_END explicitly
        //
        RELVAL *tail = ARR_TAIL(ARR(s));
        if (NOT_END(tail))
            panic (tail);
    }
    else {
        // If they are terminated, then non-REBVAL-bearing series must have
        // their terminal element as all 0 bytes (to use this check)
        //
        REBCNT len = SER_LEN(s);
        REBCNT wide = SER_WIDE(s);
        REBCNT n;
        for (n = 0; n < wide; n++) {
            if (0 != SER_DATA_RAW(s)[(len * wide) + n])
                panic (s);
        }
    }
}


//
//  Assert_Series_Core: C
//
void Assert_Series_Core(REBSER *s)
{
    if (IS_FREE_NODE(s))
        panic (s);

    assert(
        GET_SER_INFO(s, SERIES_INFO_0_IS_TRUE) // @ NODE_FLAG_NODE
        and NOT_SER_INFO(s, SERIES_INFO_1_IS_FALSE) // @ NOT(NODE_FLAG_FREE)
        and NOT_SER_INFO(s, SERIES_INFO_7_IS_FALSE) // @ NODE_FLAG_CELL
    );

    assert(SER_LEN(s) < SER_REST(s));

    Assert_Series_Term_Core(s);
}


//
//  Panic_Series_Debug: C
//
// The goal of this routine is to progressively reveal as much diagnostic
// information about a series as possible.  Since the routine will ultimately
// crash anyway, it is okay if the diagnostics run code which might be
// risky in an unstable state...though it is ideal if it can run to the end
// so it can trigger Address Sanitizer or Valgrind's internal stack dump.
//
ATTRIBUTE_NO_RETURN void Panic_Series_Debug(REBSER *s)
{
    fflush(stdout);
    fflush(stderr);

    if (s->header.bits & NODE_FLAG_MANAGED)
        fprintf(stderr, "managed");
    else
        fprintf(stderr, "unmanaged");

    fprintf(stderr, " series");

  #if defined(DEBUG_COUNT_TICKS)
    fprintf(stderr, " was likely ");
    if (s->header.bits & NODE_FLAG_FREE)
        fprintf(stderr, "freed");
    else
        fprintf(stderr, "created");

    fprintf(
        stderr, " during evaluator tick: %lu\n", cast(unsigned long, s->tick)
    );
  #else
    fprintf(stderr, " has no tick tracking (see DEBUG_COUNT_TICKS)\n");
  #endif

    fflush(stderr);

  #if defined(DEBUG_SERIES_ORIGINS)
    if (*s->guard == 1020) // should make valgrind or asan alert
        panic ("series guard didn't trigger ASAN/valgrind trap");

    panic (
        "series guard didn't trigger ASAN/Valgrind trap\n" \
        "either not a REBSER, or you're not running ASAN/Valgrind\n"
    );
  #else
    panic ("Executable not built with DEBUG_SERIES_ORIGINS, no more info");
  #endif
}

#endif
