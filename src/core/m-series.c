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
//  Extend_Series: C
//
// Extend a series at its end without affecting its tail index.
//
void Extend_Series(REBSER *s, REBLEN delta)
{
    REBLEN used_old = SER_USED(s);
    EXPAND_SERIES_TAIL(s, delta);
    SET_SERIES_LEN(s, used_old);
}


//
//  Copy_Series_Core: C
//
// Copy underlying series that *isn't* an "array" (such as STRING!, BINARY!,
// BITSET!, VECTOR!...).  Includes the terminator.
//
// Use Copy_Array routines (which specify Shallow, Deep, etc.) for greater
// detail needed when expressing intent for Rebol Arrays.
//
// The reason this can be used on strings or binaries is because it copies
// from the head position.  Copying from a non-head position might be in the
// middle of a UTF-8 codepoint, hence a string series aliased as a binary
// could only have its copy used in a BINARY!.
//
REBSER *Copy_Series_Core(const REBSER *s, REBFLGS flags)
{
    assert(not IS_SER_ARRAY(s));

    REBLEN used = SER_USED(s);
    REBSER *copy;

    // !!! Semantics of copying hasn't really covered how flags will be
    // propagated.  This includes locks, etc.  But the string flag needs
    // to be copied, for sure.
    //
    if (IS_SER_UTF8(s)) {
        //
        // Note: If the string was a symbol (aliased via AS) it will lose
        // that information.
        //
        copy = Make_String_Core(used, flags);
        SET_SERIES_USED(copy, used);
        *SER_TAIL(REBYTE, copy) = '\0';
        mutable_LINK(Bookmarks, copy) = nullptr;  // !!! Review: copy these?
        copy->misc.length = s->misc.length;
    }
    else if (SER_WIDE(s) == 1) {  // non-string BINARY!
        copy = Make_Series(
            used + 1,  // term space
            FLAG_FLAVOR_BYTE(SER_FLAVOR(s)) | flags
        );
        SET_SERIES_USED(copy, used);
    }
    else {
        copy = Make_Series(used, FLAG_FLAVOR_BYTE(SER_FLAVOR(s)) | flags);
        SET_SERIES_USED(copy, used);
    }

    memcpy(SER_DATA(copy), SER_DATA(s), used * SER_WIDE(s));

    ASSERT_SERIES_TERM_IF_NEEDED(copy);
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
REBSER *Copy_Series_At_Len_Extra(
    const REBSER *s,
    REBLEN index,
    REBLEN len,
    REBLEN extra,
    REBFLGS flags
){
    assert(not IS_SER_ARRAY(s));

    REBLEN capacity = len + extra;
    if (SER_WIDE(s) == 1)
        ++capacity;
    REBSER *copy = Make_Series(capacity, flags);
    assert(SER_WIDE(s) == SER_WIDE(copy));
    memcpy(
        SER_DATA(copy),
        SER_DATA(s) + index * SER_WIDE(s),
        len * SER_WIDE(s)  // !!! Review if +1 copying terminator is worth it
    );
    SET_SERIES_USED(copy, len);
    TERM_SERIES_IF_NECESSARY(copy);
    return copy;
}


//
//  Remove_Series_Units: C
//
// Remove a series of values (bytes, longs, reb-vals) from the
// series at the given index.
//
void Remove_Series_Units(REBSER *s, REBSIZ offset, REBLEN quantity)
{
    if (quantity == 0)
        return;

    bool is_dynamic = IS_SER_DYNAMIC(s);
    REBLEN used_old = SER_USED(s);

    REBLEN start = offset * SER_WIDE(s);

    // Optimized case of head removal.  For a dynamic series this may just
    // add "bias" to the head...rather than move any bytes.

    if (is_dynamic and offset == 0) {
        if (cast(REBLEN, quantity) > used_old)
            quantity = used_old;

        s->content.dynamic.used -= quantity;
        if (s->content.dynamic.used == 0) {
            // Reset bias to zero:
            quantity = SER_BIAS(s);
            SER_SET_BIAS(s, 0);
            s->content.dynamic.rest += quantity;
            s->content.dynamic.data -= SER_WIDE(s) * quantity;
        }
        else {
            // Add bias to head:
            unsigned int bias;
            if (REB_U32_ADD_OF(SER_BIAS(s), quantity, &bias))
                fail (Error_Overflow_Raw());

            if (bias > 0xffff) { // 16-bit, simple SER_ADD_BIAS could overflow
                char *data = s->content.dynamic.data;

                data += SER_WIDE(s) * quantity;
                s->content.dynamic.data -= SER_WIDE(s) * SER_BIAS(s);

                s->content.dynamic.rest += SER_BIAS(s);
                SER_SET_BIAS(s, 0);

                memmove(
                    s->content.dynamic.data,
                    data,
                    SER_USED(s) * SER_WIDE(s)
                );
            }
            else {
                SER_SET_BIAS(s, bias);
                s->content.dynamic.rest -= quantity;
                s->content.dynamic.data += SER_WIDE(s) * quantity;
                if ((start = SER_BIAS(s)) != 0) {
                    // If more than half biased:
                    if (start >= MAX_SERIES_BIAS or start > SER_REST(s))
                        Unbias_Series(s, true);
                }
            }
        }
        TERM_SERIES_IF_NECESSARY(s);  // !!! Review doing more elegantly
        return;
    }

    if (offset >= used_old)
        return;

    // Clip if past end and optimize the remove operation:

    if (quantity + offset >= used_old) {
        SET_SERIES_USED(s, offset);
        return;
    }

    REBLEN total = SER_USED(s) * SER_WIDE(s);

    REBYTE *data = SER_DATA(s) + start;
    memmove(
        data,
        data + (quantity * SER_WIDE(s)),
        total - (start + (quantity * SER_WIDE(s)))
    );
    SET_SERIES_USED(s, used_old - quantity);
}


//
//  Remove_Any_Series_Len: C
//
// Remove a series of values (bytes, longs, reb-vals) from the
// series at the given index.
//
void Remove_Any_Series_Len(REBVAL *v, REBLEN index, REBINT len)
{
    if (ANY_BINSTR(v)) {  // ANY-STRING! or BINARY! series
        //
        // The complicated logic in Modify_String_Or_Binary() handles many
        // aspects of the removal; e.g. updating "bookmarks" that help find
        // indexes in UTF-8 strings, as well as checking to make sure that
        // modifications of binaries that are aliases of strings do not make
        // invalid UTF-8.  Factor better...but don't repeat that work here.
        //
        DECLARE_LOCAL (temp);
        Init_Any_Series_At(temp, VAL_TYPE(v), VAL_SERIES(v), index);
        Modify_String_Or_Binary(
            temp,
            SYM_CHANGE,
            Lib(NULL),
            AM_PART,
            len,
            1  // dups
        );
    }
    else  // ANY-ARRAY! is more straightforward
        Remove_Series_Units(VAL_SERIES_ENSURE_MUTABLE(v), index, len);

    ASSERT_SERIES_TERM_IF_NEEDED(VAL_SERIES(v));
}


//
//  Unbias_Series: C
//
// Reset series bias.
//
void Unbias_Series(REBSER *s, bool keep)
{
    REBLEN bias = SER_BIAS(s);
    if (bias == 0)
        return;

    REBYTE *data = cast(REBYTE*, s->content.dynamic.data);

    SER_SET_BIAS(s, 0);
    s->content.dynamic.rest += bias;
    s->content.dynamic.data -= SER_WIDE(s) * bias;

    if (keep) {
        memmove(s->content.dynamic.data, data, SER_USED(s) * SER_WIDE(s));
        TERM_SERIES_IF_NECESSARY(s);
    }
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
        Unbias_Series(a, false);
    SET_SERIES_LEN(a, 0);
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
        memset(s->content.dynamic.data, 0, SER_REST(s) * SER_WIDE(s));
    }
    else
        memset(cast(REBYTE*, &s->content), 0, sizeof(s->content));

    TERM_SERIES_IF_NECESSARY(s);
}


//
//  Reset_Buffer: C
//
// Setup to reuse a shared buffer. Expand it if needed.
//
// NOTE: The length will be set to the supplied value, but the series will
// not be terminated.
//
REBYTE *Reset_Buffer(REBSER *buf, REBLEN len)
{
    if (buf == NULL)
        panic ("buffer not yet allocated");

    SET_SERIES_LEN(buf, 0);
    Unbias_Series(buf, true);
    Expand_Series(buf, 0, len); // sets new tail

    return SER_DATA(buf);
}


#if !defined(NDEBUG)

//
//  Assert_Series_Term_Core: C
//
void Assert_Series_Term_Core(const REBSER *s)
{
    if (IS_SER_ARRAY(s)) {
      #ifdef DEBUG_TERM_ARRAYS
        if (IS_SER_DYNAMIC(s)) {
            const RELVAL *tail = ARR_TAIL(ARR(s));
            if (not (tail->header.bits & NODE_FLAG_CELL))
                panic (s);
            if (not IS_CELL_FREE(tail))
                panic (tail);
        }
      #endif
    }
    else if (SER_WIDE(s) == 1) {
        const REBYTE *tail = BIN_TAIL(BIN(s));
        if (IS_SER_UTF8(s)) {
            if (*tail != '\0')
                panic (s);
        }
        else {
          #if !defined(NDEBUG)
            if (*tail != BINARY_BAD_UTF8_TAIL_BYTE && *tail != '\0')
                panic (s);
          #endif
        }
    }
}


//
//  Assert_Series_Core: C
//
void Assert_Series_Core(const REBSER *s)
{
    if (IS_FREE_NODE(s))
        panic (s);

    assert(SER_FLAVOR(s) != FLAVOR_TRASH);
    assert(SER_USED(s) <= SER_REST(s));

    Assert_Series_Term_Core(s);
}

#endif


#if defined(DEBUG_FANCY_PANIC)

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

    if (s->leader.bits & NODE_FLAG_MANAGED)
        fprintf(stderr, "managed");
    else
        fprintf(stderr, "unmanaged");

    fprintf(stderr, " series");

  #if defined(DEBUG_COUNT_TICKS)
    fprintf(stderr, " was likely ");
    if (s->leader.bits & NODE_FLAG_FREE)
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
    #if defined(TO_WINDOWS)
        printf("\nCALL STACK WHERE ALLOCATED:\n");
        Print_Winstack_Debug(s->guard);

        printf("\nCURRENT CALL STACK:\n");
        Print_Winstack_Debug(Make_Winstack_Debug());
    #else
        if (*s->guard == 1020)  // should make valgrind or asan alert
            panic ("series guard didn't trigger ASAN/valgrind trap");
    #endif

    panic (
        "series guard didn't trigger ASAN/Valgrind trap\n" \
        "either not a REBSER, or you're not running ASAN/Valgrind\n"
    );
  #else
    panic ("Executable not built with DEBUG_SERIES_ORIGINS, no more info");
  #endif
}

#endif  // !defined(NDEBUG)
