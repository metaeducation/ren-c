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
Array* Copy_Array_At_Extra_Shallow(
    const Array* original,
    REBLEN index,
    REBSPC *specifier,
    REBLEN extra,
    Flags flags
){
    REBLEN len = Array_Len(original);

    if (index > len)
        return Make_Array_For_Copy(extra, flags, original);

    len -= index;

    Array* copy = Make_Array_For_Copy(len + extra, flags, original);
    Set_Series_Len(copy, len);

    Cell(const*) src = Array_At(original, index);
    Cell(*) dest = Array_Head(copy);
    REBLEN count = 0;
    for (; count < len; ++count, ++dest, ++src)
        Derelativize(dest, src, specifier);

    return copy;
}


//
//  Copy_Array_At_Max_Shallow: C
//
// Shallow copy an array from the given index for given maximum
// length (clipping if it exceeds the array length)
//
Array* Copy_Array_At_Max_Shallow(
    const Array* original,
    REBLEN index,
    REBSPC *specifier,
    REBLEN max
){
    const Flags flags = 0;

    if (index > Array_Len(original))
        return Make_Array_For_Copy(0, flags, original);

    if (index + max > Array_Len(original))
        max = Array_Len(original) - index;

    Array* copy = Make_Array_For_Copy(max, flags, original);
    Set_Series_Len(copy, max);

    REBLEN count = 0;
    Cell(const*) src = Array_At(original, index);
    Cell(*) dest = Array_Head(copy);
    for (; count < max; ++count, ++src, ++dest)
        Derelativize(dest, src, specifier);

    return copy;
}


//
//  Copy_Values_Len_Extra_Shallow_Core: C
//
// Shallow copy the first 'len' values of `head` into a new series created to
// hold that many entries, with an optional bit of extra space at the end.
//
Array* Copy_Values_Len_Extra_Shallow_Core(
    Cell(const*) head,
    REBSPC *specifier,
    REBLEN len,
    REBLEN extra,
    Flags flags
){
    Array* a = Make_Array_Core(len + extra, flags);
    Set_Series_Len(a, len);

    REBLEN count = 0;
    Cell(const*) src = head;
    Cell(*) dest = Array_Head(a);
    for (; count < len; ++count, ++src, ++dest) {
        if (
            Is_Isotope(src)
            or VAL_TYPE_UNCHECKED(src) == REB_VOID  // allow unreadable trash
        ){
            assert(IS_VARLIST(a));  // usually not legal
        }

        Derelativize(dest, src, specifier);
    }

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
    Cell(*) v,
    Flags flags,
    REBU64 deep_types
){
    if (C_STACK_OVERFLOWING(&deep_types))
        Fail_Stack_Overflow();

    assert(flags & NODE_FLAG_MANAGED);

    // !!! Could theoretically do what COPY does and generate a new hijackable
    // identity.  There's no obvious use for this; hence not implemented.
    //
    assert(not (deep_types & FLAGIT_KIND(REB_FRAME)));

  #if DEBUG_UNREADABLE_TRASH
    if (IS_TRASH(v))  // running code below would assert
        return;
  #endif

    // !!! This used to have a distinguished `kind` but that was taken after
    // it had been dequoted, so effectively it was the `heart`.
    //
    enum Reb_Kind heart = CELL_HEART(v);

    if (deep_types & FLAGIT_KIND(heart) & TS_SERIES_OBJ) {
        //
        // Objects and series get shallow copied at minimum
        //
        Series* series;
        bool would_need_deep;

        if (ANY_CONTEXT_KIND(heart)) {
            INIT_VAL_CONTEXT_VARLIST(
                v,
                CTX_VARLIST(Copy_Context_Shallow_Managed(VAL_CONTEXT(v)))
            );
            series = CTX_VARLIST(VAL_CONTEXT(v));
            would_need_deep = true;
        }
        else if (ANY_ARRAYLIKE(v)) {
            series = Copy_Array_At_Extra_Shallow(
                VAL_ARRAY(v),
                0,  // index
                VAL_SPECIFIER(v),
                0,  // extra
                NODE_FLAG_MANAGED
            );

            // Despite their immutability, new instances of PATH! need to be
            // able to bind their word components differently from the path
            // they are copied from...which requires new cells.  (Also any
            // nested blocks or groups need to be copied deeply.)
            //
            if (ANY_SEQUENCE_KIND(heart))
                Freeze_Array_Shallow(ARR(series));

            INIT_VAL_NODE1(v, series);
            INIT_SPECIFIER(v, UNBOUND);  // copying w/specifier makes specific
            would_need_deep = true;
        }
        else if (ANY_SERIES_KIND(heart)) {
            series = Copy_Series_Core(
                VAL_SERIES(v),
                NODE_FLAG_MANAGED
            );
            INIT_VAL_NODE1(v, series);
            would_need_deep = false;
        }
        else {
            series = nullptr;
            would_need_deep = false;
        }

        // If we're going to copy deeply, we go back over the shallow
        // copied series and "clonify" the values in it.
        //
        if (would_need_deep and (deep_types & FLAGIT_KIND(heart))) {
            Cell(const*) sub_tail = Array_Tail(ARR(series));
            Cell(*) sub = Array_Head(ARR(series));
            for (; sub != sub_tail; ++sub)
                Clonify(sub, flags, deep_types);
        }
    }
    else {
        // We're not copying the value, so inherit the const bit from the
        // original value's point of view, if applicable.
        //
        if (Not_Cell_Flag(v, EXPLICITLY_MUTABLE))
            v->header.bits |= (flags & ARRAY_FLAG_CONST_SHALLOW);
    }
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
Array* Copy_Array_Core_Managed(
    const Array* original,
    REBLEN index,
    REBSPC *specifier,
    REBLEN tail,
    REBLEN extra,
    Flags flags,
    REBU64 deep_types
){
    if (index > tail) // !!! should this be asserted?
        index = tail;

    if (index > Array_Len(original)) // !!! should this be asserted?
        return Make_Array_Core(extra, flags | NODE_FLAG_MANAGED);

    assert(index <= tail and tail <= Array_Len(original));

    REBLEN len = tail - index;

    // Currently we start by making a shallow copy and then adjust it

    Array* copy = Make_Array_For_Copy(
        len + extra,
        flags | NODE_FLAG_MANAGED,
        original
    );
    Set_Series_Len(copy, len);

    Cell(const*) src = Array_At(original, index);
    Cell(*) dest = Array_Head(copy);
    REBLEN count = 0;
    for (; count < len; ++count, ++dest, ++src) {
        Clonify(
            Derelativize(dest, src, specifier),
            flags | NODE_FLAG_MANAGED,
            deep_types
        );
    }

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
Array* Copy_Rerelativized_Array_Deep_Managed(
    const Array* original,
    Action* before, // references to `before` will be changed to `after`
    Action* after
){
    const Flags flags = NODE_FLAG_MANAGED;

    Array* copy = Make_Array_For_Copy(Array_Len(original), flags, original);
    Cell(const*) src_tail = Array_Tail(original);
    Cell(const*) src = Array_Head(original);
    Cell(*) dest = Array_Head(copy);

    for (; src != src_tail; ++src, ++dest) {
        if (not IS_RELATIVE(src)) {
            Copy_Cell(dest, SPECIFIC(src));
            continue;
        }

        // All relative values under a sub-block must be relative to the
        // same function.
        //
        assert(ACT(BINDING(src)) == before);

        Copy_Cell_Header(dest, src);

        if (ANY_ARRAYLIKE(src)) {
            INIT_VAL_NODE1(
                dest,
                Copy_Rerelativized_Array_Deep_Managed(
                    VAL_ARRAY(src), before, after
                )
            );
            PAYLOAD(Any, dest).second = PAYLOAD(Any, src).second;
            INIT_SPECIFIER(dest, after); // relative binding
        }
        else {
            assert(ANY_WORD(src));
            PAYLOAD(Any, dest) = PAYLOAD(Any, src);
            INIT_SPECIFIER(dest, after);
        }

    }

    Set_Series_Len(copy, Array_Len(original));

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
Cell(*) Alloc_Tail_Array(Array* a)
{
    Expand_Series_Tail(a, 1);
    Set_Series_Len(a, Array_Len(a));
    Cell(*) last = Array_Last(a);

  #if DEBUG_ERASE_ALLOC_TAIL_CELLS
    if (not Is_Cell_Erased(last)) {
        assert(WRITABLE(last));
        Erase_Cell(last);  // helps ensure new values written
    }
  #endif

    return last;
}


//
//  Uncolor_Array: C
//
void Uncolor_Array(const Array* a)
{
    if (Is_Series_White(a))
        return; // avoid loop

    Flip_Series_To_White(a);

    Cell(const*) tail = Array_Tail(a);
    Cell(const*) v = Array_Head(a);
    for (; v != tail; ++v) {
        if (ANY_PATH(v) or ANY_ARRAY(v) or IS_MAP(v) or ANY_CONTEXT(v))
            Uncolor(v);
    }
}


//
//  Uncolor: C
//
// Clear the recusion markers for series and object trees.
//
void Uncolor(Cell(const*) v)
{
    if (Is_Isotope(v))
        return;

    if (ANY_ARRAY(v))
        Uncolor_Array(VAL_ARRAY(v));
    else if (ANY_PATH(v)) {
        REBLEN len = VAL_SEQUENCE_LEN(v);
        REBLEN i;
        DECLARE_LOCAL (temp);
        for (i = 0; i < len; ++i) {
            Cell(const*) item = VAL_SEQUENCE_AT(temp, v, i);
            Uncolor(item);
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
