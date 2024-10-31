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
//  Try_Flex_Data_Alloc: C
//
// Allocates the data array for an already allocated Flex Stub structure.
// Resets the bias and tail to zero, and sets the new width.  Flags like
// FLEX_FLAG_FIXED_SIZE are left as they were, and other fields in the
// Stub structure are untouched.
//
// This routine can thus be used for an initial construction or an operation
// like expansion.
//
// 1. Currently once a Flex becomes dynamic, it never goes back.  There is
//    no shrinking process that will pare it back to fit completely in
//    the Flex Stub if it gets small enough to do so.  This may change.
//
// 2. One benefit of using a bespoke pooled allocator is that Flex know
//    how much extra space their is in the allocation block, and can use
//    it as expansion capacity.
//
// 3. When R3-Alpha was asked to make an allocation too big to fit in any
//    of the preallocated-size pools, it didn't just malloc() the big size.
//    It did some second-guessing to align to 2Kb boundaries (or choose a
//    power of 2, if requested).  It's nearly certain that the 90's era
//    experience informing this code is outdated, and should be reviewed,
//    likely replacing the allocator completely.  But what's here runs.
//
// 4. The Bias component was once shared with other flags in R3-Alpha, when
//    Flex were smaller (6 pointers vs. 8).  This may become an interesting
//    feature in the future again.  So since Set_Flex_Bias() uses bit masks
//    on an existing value, clear out the whole value for starters.
//
bool Try_Flex_Data_Alloc(Flex* s, REBLEN capacity) {
    assert(Get_Stub_Flag(s, DYNAMIC));  // once set, never shrinks [1]

    Byte wide = Flex_Wide(s);
    assert(wide != 0);

    if (cast(REBU64, capacity) * wide > INT32_MAX)  // R3-Alpha said "too big"
        return false;

    Size size;  // size of allocation, possibly bigger than we need [2]

    PoolId pool_id = Pool_Id_For_Size(capacity * wide);

    if (pool_id < SYSTEM_POOL) {  // a pool is designated for this size range
        s->content.dynamic.data = cast(char*, Try_Alloc_Pooled(pool_id));
        if (not s->content.dynamic.data)
            return false;

        size = g_mem.pools[pool_id].wide;
        assert(size >= capacity * wide);

        Clear_Flex_Flag(s, POWER_OF_2);  // fits in a pool, no rounding
    }
    else {  // too big for a pool, second-guess the requested size [3]
        size = capacity * wide;
        if (Get_Flex_Flag(s, POWER_OF_2)) {
            Size size2 = 2048;
            while (size2 < size)
                size2 *= 2;
            size = size2;

            if (size % wide == 0)  // even divisibility by item width
                Clear_Flex_Flag(s, POWER_OF_2);  // flag isn't necessary
        }

        s->content.dynamic.data = Try_Alloc_Memory_N(char, size);
        if (not s->content.dynamic.data)
            return false;

        g_mem.pools[SYSTEM_POOL].has += size;
        g_mem.pools[SYSTEM_POOL].free++;
    }

    if (Is_Flex_Biased(s))
        s->content.dynamic.bonus.bias = 0;  // fully clear value [4]
    else {
        // Leave corrupt, or as existing bonus (if called in Expand_Flex())
    }

    /*assert(size % wide == 0);*/  // allow irregular sizes
    s->content.dynamic.rest = size / wide;  // extra capacity in units

    s->content.dynamic.used = 0;  // all series start zero length

    if ((g_gc.depletion -= size) <= 0)  // should we trigger garbage collect?
        Set_Trampoline_Flag(RECYCLE);  // queue it to run on next evaluation

    assert(Flex_Total(s) <= size);  // irregular widths won't use all space
    return true;
}


//
//  Extend_Flex_If_Necessary: C
//
// Extend a series at its end without affecting its tail index.
//
void Extend_Flex_If_Necessary(Flex* f, REBLEN delta)
{
    REBLEN used_old = Flex_Used(f);
    Expand_Flex_Tail(f, delta);
    Set_Flex_Len(f, used_old);
}


//
//  Copy_Flex_Core: C
//
// Copy underlying Flex that *isn't* an "array" (such as STRING!, BINARY!,
// BITSET!...).  Includes the terminator.
//
// Use Copy_Array routines (which specify Shallow, Deep, etc.) for greater
// detail needed when expressing intent for Rebol Arrays.
//
// The reason this can be used on String or Binary is because it copies
// from the head position.  Copying from a non-head position might be in the
// middle of a UTF-8 codepoint, hence a String Flex aliased as a Binary
// could only have its copy used in a BINARY!.
//
Flex* Copy_Flex_Core(Flags flags, const Flex* f)
{
    if (Flavor_From_Flags(flags) == FLAVOR_0)
        flags |= FLAG_FLAVOR_BYTE(Stub_Flavor(f));  // use source's type
    else
        assert(Flavor_From_Flags(flags) == Stub_Flavor(f));

    assert(not Stub_Holds_Cells(f));

    REBLEN used = Flex_Used(f);
    Flex* copy;

    // !!! Semantics of copying hasn't really covered how flags will be
    // propagated.  This includes locks, etc.  But the string flag needs
    // to be copied, for sure.
    //
    if (Is_Stub_String(f)) {
        //
        // Note: If the string was a symbol (aliased via AS) it will lose
        // that information.
        //
        copy = Make_String_Core(flags, used);
        Set_Flex_Used(copy, used);
        *Flex_Tail(Byte, copy) = '\0';
        LINK(Bookmarks, copy) = nullptr;  // !!! Review: copy these?
        copy->misc.length = f->misc.length;
    }
    else if (Flex_Wide(f) == 1) {  // non-string BINARY!
        copy = Make_Flex_Core(flags, used + 1);  // term space
        Set_Flex_Used(copy, used);
    }
    else {
        copy = Make_Flex_Core(flags, used);
        Set_Flex_Used(copy, used);
    }

    memcpy(Flex_Data(copy), Flex_Data(f), used * Flex_Wide(f));

    Assert_Flex_Term_If_Needed(copy);
    return copy;
}


//
//  Copy_Flex_At_Len_Extra: C
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
Flex* Copy_Flex_At_Len_Extra(
    Flags flags,
    const Flex* f,
    REBLEN index,
    REBLEN len,
    REBLEN extra
){
    assert(not Stub_Holds_Cells(f));

    REBLEN capacity = len + extra;
    if (Flex_Wide(f) == 1)
        ++capacity;
    Flex* copy = Make_Flex_Core(flags, capacity);
    assert(Flex_Wide(f) == Flex_Wide(copy));
    memcpy(
        Flex_Data(copy),
        Flex_Data(f) + index * Flex_Wide(f),
        len * Flex_Wide(f)  // !!! Would +1 copying terminator be worth it?
    );
    Set_Flex_Used(copy, len);
    Term_Flex_If_Necessary(copy);
    return copy;
}


//
//  Remove_Flex_Units: C
//
// Remove a series of values (bytes, longs, reb-vals) from the
// series at the given index.
//
void Remove_Flex_Units(Flex* f, Size byteoffset, REBLEN quantity)
{
    if (quantity == 0)
        return;

    bool is_dynamic = Get_Stub_Flag(f, DYNAMIC);
    REBLEN used_old = Flex_Used(f);

    REBLEN start = byteoffset * Flex_Wide(f);

    // Optimized case of head removal.  For a dynamic series this may just
    // add "bias" to the head...rather than move any bytes.

    if (is_dynamic and byteoffset == 0) {
        if (quantity > used_old)
            quantity = used_old;

        f->content.dynamic.used -= quantity;
        if (f->content.dynamic.used == 0) {
            // Reset bias to zero:
            quantity = Flex_Bias(f);
            Set_Flex_Bias(f, 0);
            f->content.dynamic.rest += quantity;
            f->content.dynamic.data -= Flex_Wide(f) * quantity;
        }
        else {
            // Add bias to head:
            unsigned int bias;
            if (REB_U32_ADD_OF(Flex_Bias(f), quantity, &bias))
                fail (Error_Overflow_Raw());

            if (bias > 0xffff) { // 16-bit, simple Add_Flex_Bias could overflow
                char *data = f->content.dynamic.data;

                data += Flex_Wide(f) * quantity;
                f->content.dynamic.data -= Flex_Wide(f) * Flex_Bias(f);

                f->content.dynamic.rest += Flex_Bias(f);
                Set_Flex_Bias(f, 0);

                memmove(
                    f->content.dynamic.data,
                    data,
                    Flex_Used(f) * Flex_Wide(f)
                );
            }
            else {
                Set_Flex_Bias(f, bias);
                f->content.dynamic.rest -= quantity;
                f->content.dynamic.data += Flex_Wide(f) * quantity;
                if ((start = Flex_Bias(f)) != 0) {
                    // If more than half biased:
                    if (start >= MAX_FLEX_BIAS or start > Flex_Rest(f))
                        Unbias_Flex(f, true);
                }
            }
        }
        Term_Flex_If_Necessary(f);  // !!! Review doing more elegantly
        return;
    }

    if (byteoffset >= used_old)
        return;

    // Clip if past end and optimize the remove operation:

    if (quantity + byteoffset >= used_old) {
        Set_Flex_Used(f, byteoffset);
        return;
    }

    REBLEN total = Flex_Used(f) * Flex_Wide(f);

    Byte* data = Flex_Data(f) + start;
    memmove(
        data,
        data + (quantity * Flex_Wide(f)),
        total - (start + (quantity * Flex_Wide(f)))
    );
    Set_Flex_Used(f, used_old - quantity);
}


//
//  Remove_Any_Series_Len: C
//
// Remove a series of values (bytes, longs, reb-vals) from the
// series at the given index.
//
void Remove_Any_Series_Len(Value* v, REBLEN index, REBINT len)
{
    if (Any_String(v) or Is_Binary(v)) {
        //
        // The complicated logic in Modify_String_Or_Binary() handles many
        // aspects of the removal; e.g. updating "bookmarks" that help find
        // indexes in UTF-8 strings, as well as checking to make sure that
        // modifications of binaries that are aliases of strings do not make
        // invalid UTF-8.  Factor better...but don't repeat that work here.
        //
        DECLARE_VALUE (temp);
        Init_Series_At(
            temp,
            Cell_Heart_Ensure_Noquote(v),
            Cell_Flex(v),
            index
        );
        Modify_String_Or_Binary(
            temp,
            SYM_CHANGE,
            Lib(VOID),
            AM_PART,
            len,
            1  // dups
        );
    }
    else  // ANY-LIST? is more straightforward
        Remove_Flex_Units(Cell_Flex_Ensure_Mutable(v), index, len);

    Assert_Flex_Term_If_Needed(Cell_Flex(v));
}


//
//  Unbias_Flex: C
//
// Flex allows the fast removal of data from the head (like a deque) by
// adjusting its data pointer forward in the allocated memory it received.
// How much it adjusted has to be kept track of (otherwise it couldn't subtract
// it out to get the original pointer to free it).
//
// This resets the data pointer so that it points to the original allocation.
//
void Unbias_Flex(Flex* f, bool keep)
{
    REBLEN bias = Flex_Bias(f);
    if (bias == 0)
        return;

    Byte* data = cast(Byte*, f->content.dynamic.data);

    Set_Flex_Bias(f, 0);
    f->content.dynamic.rest += bias;
    f->content.dynamic.data -= Flex_Wide(f) * bias;

    if (keep) {
        memmove(f->content.dynamic.data, data, Flex_Used(f) * Flex_Wide(f));
        Term_Flex_If_Necessary(f);
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
    if (Get_Stub_Flag(a, DYNAMIC))
        Unbias_Flex(a, false);
    Set_Flex_Len(a, 0);
}


//
//  Clear_Flex: C
//
// Clear an entire Flex to zero.  Resets bias and tail.
// The tail is reset to zero.
//
void Clear_Flex(Flex* f)
{
    assert(!Is_Flex_Read_Only(f));

    if (Get_Stub_Flag(f, DYNAMIC)) {
        Unbias_Flex(f, false);
        memset(f->content.dynamic.data, 0, Flex_Rest(f) * Flex_Wide(f));
    }
    else
        memset(cast(Byte*, &f->content), 0, sizeof(f->content));
}


//
//  Reset_Buffer: C
//
// Setup to reuse a shared buffer. Expand it if needed.
//
// NOTE: The length will be set to the supplied value, but the series will
// not be terminated.
//
Byte* Reset_Buffer(Flex* buf, REBLEN len)
{
    if (buf == NULL)
        panic ("buffer not yet allocated");

    Set_Flex_Len(buf, 0);
    Unbias_Flex(buf, true);
    Expand_Flex(buf, 0, len); // sets new tail

    return Flex_Data(buf);
}


#if !defined(NDEBUG)

//
//  Assert_Flex_Term_Core: C
//
void Assert_Flex_Term_Core(const Flex* f)
{
    if (Stub_Holds_Cells(f)) {
      #if DEBUG_POISON_FLEX_TAILS
        if (Get_Stub_Flag(f, DYNAMIC)) {
            const Cell* tail = Array_Tail(x_cast(Array*, f));
            if (not Is_Cell_Poisoned(tail))
                panic (tail);
        }
      #endif
    }
    else if (Flex_Wide(f) == 1) {
        const Byte* tail = Binary_Tail(c_cast(Binary*, f));
        if (Is_Stub_String(f)) {
            if (*tail != '\0')
                panic (f);
        }
        else {
          #if DEBUG_POISON_FLEX_TAILS
            if (*tail != BINARY_BAD_UTF8_TAIL_BYTE && *tail != '\0')
                panic (f);
          #endif
        }
    }
}


//
//  Assert_Flex_Basics_Core: C
//
void Assert_Flex_Basics_Core(const Flex* f)
{
    if (Not_Node_Readable(f))
        panic (f);

    assert(FLAVOR_BYTE(f) != FLAVOR_0);
    assert(FLAVOR_BYTE(f) < FLAVOR_MAX);

    assert(Flex_Used(f) <= Flex_Rest(f));

    Assert_Flex_Term_Core(f);
}

#endif


#if DEBUG_FANCY_PANIC

//
//  Panic_Flex_Debug: C
//
// The goal of this routine is to progressively reveal as much diagnostic
// information about a Flex as possible.  Since the routine will ultimately
// crash anyway, it is okay if the diagnostics run code which might be
// risky in an unstable state...though it is ideal if it can run to the end
// so it can trigger Address Sanitizer or Valgrind's internal stack dump.
//
ATTRIBUTE_NO_RETURN void Panic_Flex_Debug(const Flex* f)
{
    fflush(stdout);
    fflush(stderr);

    if (Is_Node_Managed(f))
        fprintf(stderr, "managed");
    else
        fprintf(stderr, "unmanaged");

    fprintf(stderr, " Flex");

  #if DEBUG_COUNT_TICKS
    fprintf(stderr, " was likely ");
    if (Not_Node_Readable(f))
        fprintf(stderr, "freed");
    else
        fprintf(stderr, "created");

    fprintf(
        stderr, " during evaluator tick: %lu\n", cast(unsigned long, f->tick)
    );
  #else
    fprintf(stderr, " has no tick tracking (see DEBUG_COUNT_TICKS)\n");
  #endif

    fflush(stderr);

  #if DEBUG_FLEX_ORIGINS
    if (*f->guard == FREE_POOLUNIT_BYTE)  // should make valgrind or asan alert
        panic ("Flex guard didn't trigger ASAN/valgrind alert");

    panic (
        "Flex guard didn't trigger ASAN/Valgrind alert\n" \
        "either not a Flex, or you're not running ASAN/Valgrind\n"
    );
  #else
    panic ("Executable not built with DEBUG_FLEX_ORIGINS, no more info");
  #endif
}

#endif  // !defined(NDEBUG)
