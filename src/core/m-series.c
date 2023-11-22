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
// Copyright 2012-2017 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//

#include "sys-core.h"
#include "sys-int-funcs.h"



//
//  Extend_Series_If_Necessary: C
//
// Extend a series at its end without affecting its tail index.
//
void Extend_Series_If_Necessary(Series* s, REBLEN delta)
{
    REBLEN used_old = Series_Used(s);
    Expand_Series_Tail(s, delta);
    Set_Series_Len(s, used_old);
}


//
//  Copy_Series_Core: C
//
// Copy underlying series that *isn't* an "array" (such as STRING!, BINARY!,
// BITSET!...).  Includes the terminator.
//
// Use Copy_Array routines (which specify Shallow, Deep, etc.) for greater
// detail needed when expressing intent for Rebol Arrays.
//
// The reason this can be used on strings or binaries is because it copies
// from the head position.  Copying from a non-head position might be in the
// middle of a UTF-8 codepoint, hence a string series aliased as a binary
// could only have its copy used in a BINARY!.
//
Series* Copy_Series_Core(const Series* s, Flags flags)
{
    assert(not Is_Series_Array(s));

    REBLEN used = Series_Used(s);
    Series* copy;

    // !!! Semantics of copying hasn't really covered how flags will be
    // propagated.  This includes locks, etc.  But the string flag needs
    // to be copied, for sure.
    //
    if (Is_Series_UTF8(s)) {
        //
        // Note: If the string was a symbol (aliased via AS) it will lose
        // that information.
        //
        copy = Make_String_Core(used, flags);
        Set_Series_Used(copy, used);
        *Series_Tail(Byte, copy) = '\0';
        mutable_LINK(Bookmarks, copy) = nullptr;  // !!! Review: copy these?
        copy->misc.length = s->misc.length;
    }
    else if (Series_Wide(s) == 1) {  // non-string BINARY!
        copy = Make_Series_Core(
            used + 1,  // term space
            FLAG_FLAVOR_BYTE(Series_Flavor(s)) | flags
        );
        Set_Series_Used(copy, used);
    }
    else {
        copy = Make_Series_Core(
            used,
            FLAG_FLAVOR_BYTE(Series_Flavor(s)) | flags
        );
        Set_Series_Used(copy, used);
    }

    memcpy(Series_Data(copy), Series_Data(s), used * Series_Wide(s));

    Assert_Series_Term_If_Needed(copy);
    return copy;
}


//
//  Copy_Series_At_Len_Extra: C
//
// Copy a subseries out of a series that is not an array.  Includes the
// terminator for it.
//
// Use Copy_Array routines (which specify Shallow, Deep, etc.) for
// greater detail needed when expressing intent for Rebol Arrays.
//
// Note: This cannot be used to make a series that will be used in a string
// *unless* you are sure that the copy is on a correct UTF-8 codepoint
// boundary.  This is a low-level routine, so the caller must fix up the
// length information, or Init_Any_String() will complain.
//
Series* Copy_Series_At_Len_Extra(
    const Series* s,
    REBLEN index,
    REBLEN len,
    REBLEN extra,
    Flags flags
){
    assert(not Is_Series_Array(s));

    REBLEN capacity = len + extra;
    if (Series_Wide(s) == 1)
        ++capacity;
    Series* copy = Make_Series_Core(capacity, flags);
    assert(Series_Wide(s) == Series_Wide(copy));
    memcpy(
        Series_Data(copy),
        Series_Data(s) + index * Series_Wide(s),
        len * Series_Wide(s)  // !!! Would +1 copying terminator be worth it?
    );
    Set_Series_Used(copy, len);
    Term_Series_If_Necessary(copy);
    return copy;
}


//
//  Remove_Series_Units: C
//
// Remove a series of values (bytes, longs, reb-vals) from the
// series at the given index.
//
void Remove_Series_Units(Series* s, Size byteoffset, REBLEN quantity)
{
    if (quantity == 0)
        return;

    bool is_dynamic = Get_Series_Flag(s, DYNAMIC);
    REBLEN used_old = Series_Used(s);

    REBLEN start = byteoffset * Series_Wide(s);

    // Optimized case of head removal.  For a dynamic series this may just
    // add "bias" to the head...rather than move any bytes.

    if (is_dynamic and byteoffset == 0) {
        if (cast(REBLEN, quantity) > used_old)
            quantity = used_old;

        s->content.dynamic.used -= quantity;
        if (s->content.dynamic.used == 0) {
            // Reset bias to zero:
            quantity = Series_Bias(s);
            Set_Series_Bias(s, 0);
            s->content.dynamic.rest += quantity;
            s->content.dynamic.data -= Series_Wide(s) * quantity;
        }
        else {
            // Add bias to head:
            unsigned int bias;
            if (REB_U32_ADD_OF(Series_Bias(s), quantity, &bias))
                fail (Error_Overflow_Raw());

            if (bias > 0xffff) { // 16-bit, simple SER_ADD_BIAS could overflow
                char *data = s->content.dynamic.data;

                data += Series_Wide(s) * quantity;
                s->content.dynamic.data -= Series_Wide(s) * Series_Bias(s);

                s->content.dynamic.rest += Series_Bias(s);
                Set_Series_Bias(s, 0);

                memmove(
                    s->content.dynamic.data,
                    data,
                    Series_Used(s) * Series_Wide(s)
                );
            }
            else {
                Set_Series_Bias(s, bias);
                s->content.dynamic.rest -= quantity;
                s->content.dynamic.data += Series_Wide(s) * quantity;
                if ((start = Series_Bias(s)) != 0) {
                    // If more than half biased:
                    if (start >= MAX_SERIES_BIAS or start > Series_Rest(s))
                        Unbias_Series(s, true);
                }
            }
        }
        Term_Series_If_Necessary(s);  // !!! Review doing more elegantly
        return;
    }

    if (byteoffset >= used_old)
        return;

    // Clip if past end and optimize the remove operation:

    if (quantity + byteoffset >= used_old) {
        Set_Series_Used(s, byteoffset);
        return;
    }

    REBLEN total = Series_Used(s) * Series_Wide(s);

    Byte* data = Series_Data(s) + start;
    memmove(
        data,
        data + (quantity * Series_Wide(s)),
        total - (start + (quantity * Series_Wide(s)))
    );
    Set_Series_Used(s, used_old - quantity);
}


//
//  Remove_Any_Series_Len: C
//
// Remove a series of values (bytes, longs, reb-vals) from the
// series at the given index.
//
void Remove_Any_Series_Len(REBVAL *v, REBLEN index, REBINT len)
{
    if (Any_String(v) or Is_Binary(v)) {
        //
        // The complicated logic in Modify_String_Or_Binary() handles many
        // aspects of the removal; e.g. updating "bookmarks" that help find
        // indexes in UTF-8 strings, as well as checking to make sure that
        // modifications of binaries that are aliases of strings do not make
        // invalid UTF-8.  Factor better...but don't repeat that work here.
        //
        DECLARE_STABLE (temp);
        Init_Series_Cell_At(temp, VAL_TYPE(v), VAL_SERIES(v), index);
        Modify_String_Or_Binary(
            temp,
            SYM_CHANGE,
            Lib(VOID),
            AM_PART,
            len,
            1  // dups
        );
    }
    else  // ANY-ARRAY! is more straightforward
        Remove_Series_Units(VAL_SERIES_ENSURE_MUTABLE(v), index, len);

    Assert_Series_Term_If_Needed(VAL_SERIES(v));
}


//
//  Unbias_Series: C
//
// Reset series bias.
//
void Unbias_Series(Series* s, bool keep)
{
    REBLEN bias = Series_Bias(s);
    if (bias == 0)
        return;

    Byte* data = cast(Byte*, s->content.dynamic.data);

    Set_Series_Bias(s, 0);
    s->content.dynamic.rest += bias;
    s->content.dynamic.data -= Series_Wide(s) * bias;

    if (keep) {
        memmove(s->content.dynamic.data, data, Series_Used(s) * Series_Wide(s));
        Term_Series_If_Necessary(s);
    }
}


//
//  Reset_Array: C
//
// Reset series to empty. Reset bias, tail, and termination.
// The tail is reset to zero.
//
void Reset_Array(Array* a)
{
    if (Get_Series_Flag(a, DYNAMIC))
        Unbias_Series(a, false);
    Set_Series_Len(a, 0);
}


//
//  Clear_Series: C
//
// Clear an entire series to zero. Resets bias and tail.
// The tail is reset to zero.
//
void Clear_Series(Series* s)
{
    assert(!Is_Series_Read_Only(s));

    if (Get_Series_Flag(s, DYNAMIC)) {
        Unbias_Series(s, false);
        memset(s->content.dynamic.data, 0, Series_Rest(s) * Series_Wide(s));
    }
    else
        memset(cast(Byte*, &s->content), 0, sizeof(s->content));
}


//
//  Reset_Buffer: C
//
// Setup to reuse a shared buffer. Expand it if needed.
//
// NOTE: The length will be set to the supplied value, but the series will
// not be terminated.
//
Byte* Reset_Buffer(Series* buf, REBLEN len)
{
    if (buf == NULL)
        panic ("buffer not yet allocated");

    Set_Series_Len(buf, 0);
    Unbias_Series(buf, true);
    Expand_Series(buf, 0, len); // sets new tail

    return Series_Data(buf);
}


#if !defined(NDEBUG)

//
//  Assert_Series_Term_Core: C
//
void Assert_Series_Term_Core(const Series* s)
{
    if (Is_Series_Array(s)) {
      #if DEBUG_POISON_SERIES_TAILS
        if (Get_Series_Flag(s, DYNAMIC)) {
            const Cell* tail = Array_Tail(x_cast(Array*, s));
            if (not Is_Cell_Poisoned(tail))
                panic (tail);
        }
      #endif
    }
    else if (Series_Wide(s) == 1) {
        const Byte* tail = Binary_Tail(c_cast(Binary*, s));
        if (Is_Series_UTF8(s)) {
            if (*tail != '\0')
                panic (s);
        }
        else {
          #if DEBUG_POISON_SERIES_TAILS
            if (*tail != BINARY_BAD_UTF8_TAIL_BYTE && *tail != '\0')
                panic (s);
          #endif
        }
    }
}


//
//  Assert_Series_Basics_Core: C
//
void Assert_Series_Basics_Core(const Series* s)
{
    if (Is_Node_Free(s))
        panic (s);

    assert(Series_Flavor(s) != FLAVOR_TRASH);
    assert(Series_Used(s) <= Series_Rest(s));

    Assert_Series_Term_Core(s);
}

#endif


#if DEBUG_FANCY_PANIC

//
//  Panic_Series_Debug: C
//
// The goal of this routine is to progressively reveal as much diagnostic
// information about a series as possible.  Since the routine will ultimately
// crash anyway, it is okay if the diagnostics run code which might be
// risky in an unstable state...though it is ideal if it can run to the end
// so it can trigger Address Sanitizer or Valgrind's internal stack dump.
//
ATTRIBUTE_NO_RETURN void Panic_Series_Debug(const Series* s)
{
    fflush(stdout);
    fflush(stderr);

    if (Is_Node_Managed(s))
        fprintf(stderr, "managed");
    else
        fprintf(stderr, "unmanaged");

    fprintf(stderr, " series");

  #if DEBUG_COUNT_TICKS
    fprintf(stderr, " was likely ");
    if (Is_Node_Free(s))
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

  #if DEBUG_SERIES_ORIGINS
    if (*s->guard == FREE_POOLUNIT_BYTE)  // should make valgrind or asan alert
        panic ("series guard didn't trigger ASAN/valgrind alert");

    panic (
        "series guard didn't trigger ASAN/Valgrind alert\n" \
        "either not a series, or you're not running ASAN/Valgrind\n"
    );
  #else
    panic ("Executable not built with DEBUG_SERIES_ORIGINS, no more info");
  #endif
}

#endif  // !defined(NDEBUG)
