//
//  File: %f-blocks.c
//  Summary: "primary block series support functions"
//  Section: functional
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


//
//  Copy_Array_At_Extra_Shallow: C
//
// Shallow copy an array from the given index thru the tail.
// Additional capacity beyond what is required can be added
// by giving an `extra` count of how many value cells one needs.
//
REBARR *Copy_Array_At_Extra_Shallow(
    const REBARR *original,
    REBLEN index,
    REBSPC *specifier,
    REBLEN extra,
    REBFLGS flags
){
    REBLEN len = ARR_LEN(original);

    if (index > len)
        return Make_Array_For_Copy(extra, flags, original);

    len -= index;

    REBARR *copy = Make_Array_For_Copy(len + extra, flags, original);

    const RELVAL *src = ARR_AT(original, index);
    RELVAL *dest = ARR_HEAD(copy);
    REBLEN count = 0;
    for (; count < len; ++count, ++dest, ++src)
        Derelativize(dest, src, specifier);

    TERM_ARRAY_LEN(copy, len);

    return copy;
}


//
//  Copy_Array_At_Max_Shallow: C
//
// Shallow copy an array from the given index for given maximum
// length (clipping if it exceeds the array length)
//
REBARR *Copy_Array_At_Max_Shallow(
    const REBARR *original,
    REBLEN index,
    REBSPC *specifier,
    REBLEN max
){
    const REBFLGS flags = 0;

    if (index > ARR_LEN(original))
        return Make_Array_For_Copy(0, flags, original);

    if (index + max > ARR_LEN(original))
        max = ARR_LEN(original) - index;

    REBARR *copy = Make_Array_For_Copy(max, flags, original);

    REBLEN count = 0;
    const RELVAL *src = ARR_AT(original, index);
    RELVAL *dest = ARR_HEAD(copy);
    for (; count < max; ++count, ++src, ++dest)
        Derelativize(dest, src, specifier);

    TERM_ARRAY_LEN(copy, max);

    return copy;
}


//
//  Copy_Values_Len_Extra_Shallow_Core: C
//
// Shallow copy the first 'len' values of `head` into a new series created to
// hold that many entries, with an optional bit of extra space at the end.
//
REBARR *Copy_Values_Len_Extra_Shallow_Core(
    const RELVAL *head,
    REBSPC *specifier,
    REBLEN len,
    REBLEN extra,
    REBFLGS flags
){
    REBARR *a = Make_Array_Core(len + extra, flags);

    REBLEN count = 0;
    const RELVAL *src = head;
    RELVAL *dest = ARR_HEAD(a);
    for (; count < len; ++count, ++src, ++dest) {
        if (KIND_BYTE_UNCHECKED(src) == REB_NULLED)  // allow unreadable void
            assert(flags & ARRAY_FLAG_NULLEDS_LEGAL);

        Derelativize(dest, src, specifier);
    }

    TERM_ARRAY_LEN(a, len);
    return a;
}


//
//  Clonify: C
//
// Clone the series embedded in a value *if* it's in the given set of types
// (and if "cloning" makes sense for them, e.g. they are not simple scalars).
//
// Note: The resulting clones will be managed.  The model for lists only
// allows the topmost level to contain unmanaged values...and we *assume* the
// values we are operating on here live inside of an array.
//
void Clonify(
    REBVAL *v,
    REBFLGS flags,
    REBU64 deep_types
){
    if (C_STACK_OVERFLOWING(&deep_types))
        Fail_Stack_Overflow();

    assert(flags & NODE_FLAG_MANAGED);

    // !!! Could theoretically do what COPY does and generate a new hijackable
    // identity.  There's no obvious use for this; hence not implemented.
    //
    assert(not (deep_types & FLAGIT_KIND(REB_ACTION)));

    // !!! It may be possible to do this faster/better, the impacts on higher
    // quoting levels could be incurring more cost than necessary...but for
    // now err on the side of correctness.  Unescape the value while cloning
    // and then escape it back.
    //
    REBLEN num_quotes = VAL_NUM_QUOTES(v);
    Dequotify(v);

    enum Reb_Kind kind = CELL_KIND(cast(REBCEL(const*), v));
    assert(kind < REB_MAX_PLUS_MAX); // we dequoted it (pseudotypes ok)

    if (deep_types & FLAGIT_KIND(kind) & TS_SERIES_OBJ) {
        //
        // Objects and series get shallow copied at minimum
        //
        REBSER *series;
        if (ANY_CONTEXT_KIND(kind)) {
            INIT_VAL_CONTEXT_VARLIST(
                v,
                CTX_VARLIST(Copy_Context_Shallow_Managed(VAL_CONTEXT(v)))
            );
            series = SER(CTX_VARLIST(VAL_CONTEXT(v)));
        }
        else if (ANY_PATH_KIND(kind)) {
            REBNOD *n = VAL_NODE(v);
            assert(not (FIRST_BYTE(n->header.bits) & NODE_BYTEMASK_0x01_CELL));
            series = SER(
                Copy_Array_At_Extra_Shallow(
                    ARR(n),
                    0,  // index
                    VAL_SPECIFIER(v),
                    0,  // extra
                    NODE_FLAG_MANAGED
                )
            );

            Freeze_Array_Shallow(ARR(series));

            INIT_VAL_NODE(v, series);
            INIT_BINDING(v, UNBOUND);  // copying w/specifier makes specific
        }
        else if (IS_SER_ARRAY(VAL_SERIES(v))) {
            series = SER(
                Copy_Array_At_Extra_Shallow(
                    VAL_ARRAY(v),
                    0,  // !!! what if VAL_INDEX() is nonzero?
                    VAL_SPECIFIER(v),
                    0,
                    NODE_FLAG_MANAGED
                )
            );

            INIT_VAL_NODE(v, series);
            INIT_BINDING(v, UNBOUND);  // copying w/specifier makes specific
        }
        else {
            series = Copy_Sequence_Core(
                VAL_SERIES(v),
                NODE_FLAG_MANAGED
            );
            INIT_VAL_NODE(v, series);
        }

        // If we're going to copy deeply, we go back over the shallow
        // copied series and "clonify" the values in it.
        //
        if (deep_types & FLAGIT_KIND(kind) & TS_ARRAYS_OBJ) {
            REBVAL *sub = SPECIFIC(ARR_HEAD(ARR(series)));
            for (; NOT_END(sub); ++sub)
                Clonify(sub, flags, deep_types);
        }
    }
    else {
        // We're not copying the value, so inherit the const bit from the
        // original value's point of view, if applicable.
        //
        if (NOT_CELL_FLAG(v, EXPLICITLY_MUTABLE))
            v->header.bits |= (flags & ARRAY_FLAG_CONST_SHALLOW);
    }

    Quotify(v, num_quotes);
}


//
//  Copy_Array_Core_Managed: C
//
// Copy a block, copy specified values, deeply if indicated.
//
// To avoid having to do a second deep walk to add managed bits on all series,
// the resulting array will already be deeply under GC management, and hence
// cannot be freed with Free_Unmanaged_Series().
//
REBARR *Copy_Array_Core_Managed(
    const REBARR *original,
    REBLEN index,
    REBSPC *specifier,
    REBLEN tail,
    REBLEN extra,
    REBFLGS flags,
    REBU64 deep_types
){
    if (index > tail) // !!! should this be asserted?
        index = tail;

    if (index > ARR_LEN(original)) // !!! should this be asserted?
        return Make_Array_Core(extra, flags | NODE_FLAG_MANAGED);

    assert(index <= tail and tail <= ARR_LEN(original));

    REBLEN len = tail - index;

    // Currently we start by making a shallow copy and then adjust it

    REBARR *copy = Make_Array_For_Copy(
        len + extra,
        flags | NODE_FLAG_MANAGED,
        original
    );

    const RELVAL *src = ARR_AT(original, index);
    RELVAL *dest = ARR_HEAD(copy);
    REBLEN count = 0;
    for (; count < len; ++count, ++dest, ++src) {
        Clonify(
            Derelativize(dest, src, specifier),
            flags | NODE_FLAG_MANAGED,
            deep_types
        );
    }

    TERM_ARRAY_LEN(copy, len);

    return copy;
}


//
//  Copy_Rerelativized_Array_Deep_Managed: C
//
// The invariant of copying in general is that when you are done with the
// copy, there are no relative values in that copy.  One exception to this
// is the deep copy required to make a relative function body in the first
// place (which it currently does in two passes--a normal deep copy followed
// by a relative binding).  The other exception is when a relativized
// function body is copied to make another relativized function body.
//
// This is specialized logic for the latter case.  It's constrained enough
// to be simple (all relative values are known to be relative to the same
// function), and the feature is questionable anyway.  So it's best not to
// further complicate ordinary copying with a parameterization to copy
// and change all the relative binding information from one function's
// paramlist to another.
//
REBARR *Copy_Rerelativized_Array_Deep_Managed(
    const REBARR *original,
    REBACT *before, // references to `before` will be changed to `after`
    REBACT *after
){
    const REBFLGS flags = NODE_FLAG_MANAGED;

    REBARR *copy = Make_Array_For_Copy(ARR_LEN(original), flags, original);
    const RELVAL *src = ARR_HEAD(original);
    RELVAL *dest = ARR_HEAD(copy);

    for (; NOT_END(src); ++src, ++dest) {
        if (not IS_RELATIVE(src)) {
            Move_Value(dest, SPECIFIC(src));
            continue;
        }

        // All relative values under a sub-block must be relative to the
        // same function.
        //
        assert(VAL_RELATIVE(src) == before);

        Move_Value_Header(dest, src);

        if (ANY_ARRAY_OR_PATH(src)) {
            INIT_VAL_NODE(
                dest,
                Copy_Rerelativized_Array_Deep_Managed(
                    VAL_ARRAY(src), before, after
                )
            );
            PAYLOAD(Any, dest).second = PAYLOAD(Any, src).second;
            INIT_BINDING(dest, after); // relative binding
        }
        else {
            assert(ANY_WORD(src));
            PAYLOAD(Any, dest) = PAYLOAD(Any, src);
            INIT_BINDING(dest, after);
        }

    }

    TERM_ARRAY_LEN(copy, ARR_LEN(original));

    return copy;
}


//
//  Alloc_Tail_Array: C
//
// Append a REBVAL-size slot to Rebol Array series at its tail.
// Will use existing memory capacity already in the series if it
// is available, but will expand the series if necessary.
// Returns the new value for you to initialize.
//
// Note: Updates the termination and tail.
//
RELVAL *Alloc_Tail_Array(REBARR *a)
{
    EXPAND_SERIES_TAIL(SER(a), 1);
    TERM_ARRAY_LEN(a, ARR_LEN(a));
    RELVAL *last = ARR_LAST(a);
    TRASH_CELL_IF_DEBUG(last); // !!! was an END marker, good enough?
    return last;
}


//
//  Uncolor_Array: C
//
void Uncolor_Array(const REBARR *a)
{
    if (Is_Series_White(SER(a)))
        return; // avoid loop

    Flip_Series_To_White(SER(a));

    const RELVAL *v;
    for (v = ARR_HEAD(a); NOT_END(v); ++v) {
        if (ANY_PATH(v) or ANY_ARRAY(v) or IS_MAP(v) or ANY_CONTEXT(v))
            Uncolor(v);
    }
}


//
//  Uncolor: C
//
// Clear the recusion markers for series and object trees.
//
void Uncolor(const RELVAL *v)
{
    if (ANY_ARRAY(v))
        Uncolor_Array(VAL_ARRAY(v));
    else if (ANY_PATH(v)) {
        REBLEN len = VAL_PATH_LEN(v);
        REBLEN i;
        DECLARE_LOCAL (temp);
        for (i = 0; i < len; ++i) {
            REBCEL(const*) item = VAL_PATH_AT(temp, v, i);
            Uncolor(CELL_TO_VAL(item));
        }
    }
    else if (IS_MAP(v))
        Uncolor_Array(MAP_PAIRLIST(VAL_MAP(v)));
    else if (ANY_CONTEXT(v))
        Uncolor_Array(CTX_VARLIST(VAL_CONTEXT(v)));
    else {
        // Shouldn't have marked recursively any non-array series (no need)
        //
        assert(
            not ANY_SERIES(v)
            or Is_Series_White(VAL_SERIES(v))
        );
    }
}
