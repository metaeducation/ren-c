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
    REBLEN extra,
    Flags flags
){
    REBLEN len = Array_Len(original);

    if (index > len)
        return Make_Array_For_Copy(extra, flags, original);

    len -= index;

    Array* copy = Make_Array_For_Copy(len + extra, flags, original);
    Set_Series_Len(copy, len);

    const Element* src = Array_At(original, index);
    Element* dest = Array_Head(copy);
    Count count = 0;
    for (; count < len; ++count, ++dest, ++src)
        Copy_Cell(dest, src);

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
    REBLEN max
){
    const Flags flags = 0;

    if (index > Array_Len(original))
        return Make_Array_For_Copy(0, flags, original);

    if (index + max > Array_Len(original))
        max = Array_Len(original) - index;

    Array* copy = Make_Array_For_Copy(max, flags, original);
    Set_Series_Len(copy, max);

    Count count = 0;
    const Element* src = Array_At(original, index);
    Element* dest = Array_Head(copy);
    for (; count < max; ++count, ++src, ++dest)
        Copy_Cell(dest, src);

    return copy;
}


//
//  Copy_Values_Len_Extra_Shallow_Core: C
//
// Shallow copy the first 'len' values of `head` into a new series created to
// hold that many entries, with an optional bit of extra space at the end.
//
Array* Copy_Values_Len_Extra_Shallow_Core(
    const Value* head,
    REBLEN len,
    REBLEN extra,
    Flags flags
){
    Array* a = Make_Array_Core(len + extra, flags);
    Set_Series_Len(a, len);

    REBLEN count = 0;
    const Value* src = head;
    Value* dest = Series_Head(Value, a);
    for (; count < len; ++count, ++src, ++dest) {
        if (
            Is_Antiform(src)
            or VAL_TYPE_UNCHECKED(src) == REB_VOID  // allow unreadable
        ){
            assert(IS_VARLIST(a));  // usually not legal
        }

        Copy_Cell(dest, src);
    }

    return a;
}


//
//  Clonify: C
//
void Clonify(
    Value* v,
    Flags flags,
    bool deeply
){
    Clonify_And_Bind_Relative(v, flags, deeply, nullptr, nullptr);
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
    REBLEN tail,
    REBLEN extra,
    Flags flags,
    bool deeply
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

    const Element* src = Array_At(original, index);
    Element* dest = Array_Head(copy);
    REBLEN count = 0;
    for (; count < len; ++count, ++dest, ++src) {
        Copy_Cell(dest, src);
        Clonify(
            dest,
            flags | NODE_FLAG_MANAGED,
            deeply
        );
    }

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
Element* Alloc_Tail_Array(Array* a)
{
    Expand_Series_Tail(a, 1);
    Set_Series_Len(a, Array_Len(a));
    Element* last = Array_Last(a);

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

    const Element* tail = Array_Tail(a);
    const Element* v = Array_Head(a);
    for (; v != tail; ++v) {
        if (Any_Path(v) or Any_Array(v) or Is_Map(v) or Any_Context(v))
            Uncolor(v);
    }
}


//
//  Uncolor: C
//
// Clear the recusion markers for series and object trees.
//
void Uncolor(const Value* v)
{
    if (Is_Antiform(v))
        return;

    if (Any_Array(v))
        Uncolor_Array(Cell_Array(v));
    else if (Any_Path(v)) {
        REBLEN len = Cell_Sequence_Len(v);
        REBLEN i;
        DECLARE_ELEMENT (temp);
        for (i = 0; i < len; ++i) {
            Copy_Sequence_At(temp, v, i);
            Uncolor(temp);
        }
    }
    else if (Is_Map(v))
        Uncolor_Array(MAP_PAIRLIST(VAL_MAP(v)));
    else if (Any_Context(v))
        Uncolor_Array(CTX_VARLIST(VAL_CONTEXT(v)));
    else {
        // Shouldn't have marked recursively any non-array series (no need)
        //
        assert(
            not Any_Series(v)
            or Is_Series_White(Cell_Series(v))
        );
    }
}
