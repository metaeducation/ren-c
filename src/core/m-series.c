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
//  Extend_Flex: C
//
// Extend a Flex at its end without affecting its tail index.
//
void Extend_Flex(Flex* s, REBLEN delta)
{
    REBLEN len_old = Flex_Len(s);
    Expand_Flex_Tail(s, delta);
    Set_Flex_Len(s, len_old);
}


//
//  Insert_Flex: C
//
// Insert data (bytes, longs, Cells) into the Flex at the given index.
// Expand it if necessary.  Does not add a terminator to tail.
//
REBLEN Insert_Flex(
    Flex* s,
    REBLEN index,
    const Byte *data,
    REBLEN len
) {
    if (index > Flex_Len(s))
        index = Flex_Len(s);

    Expand_Flex(s, index, len); // tail += len

    memcpy(
        Flex_Data(s) + (Flex_Wide(s) * index),
        data,
        Flex_Wide(s) * len
    );

    return index + len;
}


//
//  Append_Values_Len: C
//
// Append value(s) onto the tail of an array.  The len is
// the number of units and does not include the terminator
// (which will be added).
//
void Append_Values_Len(Array* a, const Value* head, REBLEN len)
{
    REBLEN old_len = Array_Len(a);

    // updates tail, which could move data storage.
    //
    Expand_Flex_Tail(a, len);

    // `char*` casts needed: https://stackoverflow.com/q/57721104
    memcpy(
        cast(char*, Array_At(a, old_len)),
        cast(const char*, head),
        sizeof(Cell) * len
    );

    Term_Array_Len(a, Array_Len(a));
}


//
//  Copy_Non_Array_Flex_Core: C
//
// Copy any series that *isn't* an "array" (such as STRING!,
// BINARY!, BITSET!...).  Includes the terminator.
//
// Use Copy_Array routines (which specify Shallow, Deep, etc.) for
// greater detail needed when expressing intent for Rebol Arrays.
//
Flex* Copy_Non_Array_Flex_Core(Flex* s, REBFLGS flags)
{
    assert(not Is_Flex_Array(s));

    REBLEN len = Flex_Len(s);
    Flex* copy = Make_Flex_Core(len + 1, Flex_Wide(s), flags);

    memcpy(Flex_Data(copy), Flex_Data(s), len * Flex_Wide(s));
    Term_Non_Array_Flex_Len(copy, Flex_Len(s));
    return copy;
}


//
//  Copy_Non_Array_Flex_At_Len_Extra: C
//
// Copy a subseries out of a series that is not an array.
// Includes the terminator for it.
//
// Use Copy_Array routines (which specify Shallow, Deep, etc.) for
// greater detail needed when expressing intent for Rebol Arrays.
//
Flex* Copy_Non_Array_Flex_At_Len_Extra(
    Flex* s,
    REBLEN index,
    REBLEN len,
    REBLEN extra
){
    assert(not Is_Flex_Array(s));

    Flex* copy = Make_Flex(len + 1 + extra, Flex_Wide(s));
    memcpy(
        Flex_Data(copy),
        Flex_Data(s) + index * Flex_Wide(s),
        (len + 1) * Flex_Wide(s)
    );
    Term_Non_Array_Flex_Len(copy, len);
    return copy;
}


//
//  Remove_Flex: C
//
// Remove a series of values (bytes, longs, reb-vals) from the
// series at the given index.
//
void Remove_Flex(Flex* s, REBLEN index, REBINT len)
{
    if (len <= 0) return;

    bool is_dynamic = Is_Flex_Dynamic(s);
    REBLEN len_old = Flex_Len(s);

    REBLEN start = index * Flex_Wide(s);

    // Optimized case of head removal.  For a dynamic series this may just
    // add "bias" to the head...rather than move any bytes.

    if (is_dynamic and index == 0) {
        if (cast(REBLEN, len) > len_old)
            len = len_old;

        s->content.dynamic.len -= len;
        if (s->content.dynamic.len == 0) {
            // Reset bias to zero:
            len = Flex_Bias(s);
            Set_Flex_Bias(s, 0);
            s->content.dynamic.rest += len;
            s->content.dynamic.data -= Flex_Wide(s) * len;
            Term_Flex(s);
        }
        else {
            // Add bias to head:
            unsigned int bias;
            if (REB_U32_ADD_OF(Flex_Bias(s), len, &bias))
                fail (Error_Overflow_Raw());

            if (bias > 0xffff) { // 16-bit, simple Add_Flex_Bias could overflow
                Byte *data = cast(Byte*, s->content.dynamic.data);

                data += Flex_Wide(s) * len;
                s->content.dynamic.data -= Flex_Wide(s) * Flex_Bias(s);

                s->content.dynamic.rest += Flex_Bias(s);
                Set_Flex_Bias(s, 0);

                memmove(
                    s->content.dynamic.data,
                    data,
                    Flex_Len(s) * Flex_Wide(s)
                );
                Term_Flex(s);
            }
            else {
                Set_Flex_Bias(s, bias);
                s->content.dynamic.rest -= len;
                s->content.dynamic.data += Flex_Wide(s) * len;
                if ((start = Flex_Bias(s)) != 0) {
                    // If more than half biased:
                    if (start >= MAX_FLEX_BIAS or start > Flex_Rest(s))
                        Unbias_Flex(s, true);
                }
            }
        }
        return;
    }

    if (index >= len_old) return;

    // Clip if past end and optimize the remove operation:

    if (len + index >= len_old) {
        Set_Flex_Len(s, index);
        Term_Flex(s);
        return;
    }

    // The terminator is not included in the length, because termination may
    // be implicit (e.g. there may not be a full Flex_Wide() worth of data
    // at the termination location).  Use Term_Flex() instead.
    //
    REBLEN length = Flex_Len(s) * Flex_Wide(s);
    Set_Flex_Len(s, len_old - cast(REBLEN, len));
    len *= Flex_Wide(s);

    Byte *data = Flex_Data(s) + start;
    memmove(data, data + len, length - (start + len));
    Term_Flex(s);
}


//
//  Unbias_Flex: C
//
// Reset series bias.
//
void Unbias_Flex(Flex* s, bool keep)
{
    REBLEN len = Flex_Bias(s);
    if (len == 0)
        return;

    Byte *data = cast(Byte*, s->content.dynamic.data);

    Set_Flex_Bias(s, 0);
    s->content.dynamic.rest += len;
    s->content.dynamic.data -= Flex_Wide(s) * len;

    if (keep) {
        memmove(s->content.dynamic.data, data, Flex_Len(s) * Flex_Wide(s));
        Term_Flex(s);
    }
}


//
//  Reset_Non_Array_Flex: C
//
// Reset series to empty. Reset bias, tail, and termination.
// The tail is reset to zero.
//
void Reset_Non_Array_Flex(Flex* s)
{
    assert(not Is_Flex_Array(s));
    if (Is_Flex_Dynamic(s)) {
        Unbias_Flex(s, false);
        s->content.dynamic.len = 0;
        Term_Non_Array_Flex(s);
    }
    else
        Term_Non_Array_Flex_Len(s, 0);
}


//
//  Reset_Array: C
//
// Reset series to empty. Reset bias, tail, and termination.
// The tail is reset to zero.
//
void Reset_Array(Array* a)
{
    if (Is_Flex_Dynamic(a))
        Unbias_Flex(a, false);
    Term_Array_Len(a, 0);
}


//
//  Clear_Flex: C
//
// Clear an entire series to zero. Resets bias and tail.
// The tail is reset to zero.
//
void Clear_Flex(Flex* s)
{
    assert(!Is_Flex_Read_Only(s));

    if (Is_Flex_Dynamic(s)) {
        Unbias_Flex(s, false);
        CLEAR(s->content.dynamic.data, Flex_Rest(s) * Flex_Wide(s));
    }
    else
        CLEAR(cast(Byte*, &s->content), sizeof(s->content));

    Term_Flex(s);
}


//
//  Resize_Flex: C
//
// Reset series and expand it to required size.
// The tail is reset to zero.
//
void Resize_Flex(Flex* s, REBLEN size)
{
    if (Is_Flex_Dynamic(s)) {
        s->content.dynamic.len = 0;
        Unbias_Flex(s, true);
    }
    else
        Set_Flex_Len(s, 0);

    Expand_Flex_Tail(s, size);
    Set_Flex_Len(s, 0);
    Term_Flex(s);
}


#if !defined(NDEBUG)

//
//  Assert_Flex_Term_Core: C
//
void Assert_Flex_Term_Core(Flex* s)
{
    if (Is_Flex_Array(s)) {
        //
        // END values aren't canonized to zero bytes, check IS_END explicitly
        //
        Cell* tail = Array_Tail(cast_Array(s));
        if (NOT_END(tail))
            panic (tail);
    }
    else {
        // If they are terminated, then non-Cell-bearing series must have
        // their terminal element as all 0 bytes (to use this check)
        //
        REBLEN len = Flex_Len(s);
        REBLEN wide = Flex_Wide(s);
        REBLEN n;
        for (n = 0; n < wide; n++) {
            if (0 != Flex_Data(s)[(len * wide) + n])
                panic (s);
        }
    }
}


//
//  Assert_Flex_Core: C
//
void Assert_Flex_Core(Flex* s)
{
    if (IS_FREE_NODE(s))
        panic (s);

    assert(
        Get_Flex_Info(s, FLEX_INFO_0_IS_TRUE) // @ NODE_FLAG_NODE
        and Not_Flex_Info(s, FLEX_INFO_1_IS_FALSE) // @ NOT(NODE_FLAG_FREE)
        and Not_Flex_Info(s, FLEX_INFO_7_IS_FALSE) // @ NODE_FLAG_CELL
    );

    assert(Flex_Len(s) < Flex_Rest(s));

    Assert_Flex_Term_Core(s);
}


//
//  Panic_Flex_Debug: C
//
// The goal of this routine is to progressively reveal as much diagnostic
// information about a series as possible.  Since the routine will ultimately
// crash anyway, it is okay if the diagnostics run code which might be
// risky in an unstable state...though it is ideal if it can run to the end
// so it can trigger Address Sanitizer or Valgrind's internal stack dump.
//
ATTRIBUTE_NO_RETURN void Panic_Flex_Debug(Flex* s)
{
    fflush(stdout);
    fflush(stderr);

    if (s->header.bits & NODE_FLAG_MANAGED)
        fprintf(stderr, "managed");
    else
        fprintf(stderr, "unmanaged");

    fprintf(stderr, " Flex");

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

  #if defined(DEBUG_FLEX_ORIGINS)
    if (*s->guard == 1020) // should make valgrind or asan alert
        panic ("Flex guard didn't trigger ASAN/valgrind trap");

    panic (
        "Flex guard didn't trigger ASAN/Valgrind trap\n" \
        "either not a Flex Stub, or you're not running ASAN/Valgrind\n"
    );
  #else
    panic ("Executable not built with DEBUG_FLEX_ORIGINS, no more info");
  #endif
}

#endif
