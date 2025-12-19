//
//  file: %f-blocks.c
//  summary: "primary Array Flex support functions"
//  section: functional
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2024 Ren-C Open Source Contributors
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
    Flags flags,
    const Array* original,
    Index index,
    REBLEN extra
){
    REBLEN len = Array_Len(original);

    if (index > len)
        return Make_Array_For_Copy(flags, original, extra);

    len -= index;

    Array* copy = Make_Array_For_Copy(flags, original, len + extra);
    Set_Flex_Len(copy, len);

    const Element* src = Array_At(original, index);
    Element* dest = Array_Head(copy);
    Count count = 0;
    for (; count < len; ++count, ++dest, ++src)
        Copy_Cell(dest, src);

    return copy;
}


//
//  Copy_Source_At_Max_Shallow: C
//
// Shallow copy an array from the given index for given maximum
// length (clipping if it exceeds the array length)
//
Source* Copy_Source_At_Max_Shallow(
    const Source* original,
    Index index,
    REBLEN max
){
    const Flags flags = STUB_MASK_UNMANAGED_SOURCE;

    if (index > Array_Len(original))
        return cast(Source*, Make_Array_For_Copy(flags, original, 0));

    if (index + max > Array_Len(original))
        max = Array_Len(original) - index;

    Source* copy = cast(Source*, Make_Array_For_Copy(flags, original, max));
    Set_Flex_Len(copy, max);

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
// Shallow copy the first 'len' values of `head` into a new Array created to
// hold that many entries, with an optional bit of extra space at the end.
//
Array* Copy_Values_Len_Extra_Shallow_Core(
    Flags flags,
    const Stable* head,
    REBLEN len,
    REBLEN extra
){
    Array* a = Make_Array_Core(flags, len + extra);
    Set_Flex_Len(a, len);

    REBLEN count = 0;
    const Stable* src = head;
    Stable* dest = Flex_Head(Stable, a);
    for (; count < len; ++count, ++src, ++dest) {
        if (Is_Antiform(src))
            assert(Is_Stub_Varlist(a));  // usually not legal

        Copy_Cell(dest, src);
    }

    return a;
}


//
//  Clonify: C
//
Result(None) Clonify(
    Element* v,
    Flags flags,
    bool deeply
){
    return Clonify_And_Bind_Relative(v, flags, deeply, nullptr, nullptr);
}


//
//  Copy_Array_Core_Managed: C
//
// Copy a block, copy specified values, deeply if indicated.
//
// To avoid having to do a second deep walk to add managed bits on all Flexes,
// the resulting Array will already be deeply under GC management, and hence
// cannot be freed with Free_Unmanaged_Flex().
//
Result(Array*) Copy_Array_Core_Managed(  // always managed, [1]
    Flags flags,
    const Array* original,
    Index index,
    REBLEN tail,
    REBLEN extra,
    bool deeply
){
    assert(flags & BASE_FLAG_MANAGED);  // [1]

    if (index > tail) // !!! should this be asserted?
        index = tail;

    if (index > Array_Len(original)) // !!! should this be asserted?
        return Make_Array_Core(flags, extra);

    assert(index <= tail and tail <= Array_Len(original));

    REBLEN len = tail - index;

    // Currently we start by making a shallow copy and then adjust it

    Array* copy = Make_Array_For_Copy(flags, original, len + extra);
    Set_Flex_Len(copy, len);

    const Element* src = Array_At(original, index);
    Element* dest = Array_Head(copy);
    REBLEN count = 0;
    for (; count < len; ++count, ++dest, ++src) {
        Copy_Cell(dest, src);
        trap (
          Clonify(  // !!! undo previous allocations?
            dest,
            flags | BASE_FLAG_MANAGED,
            deeply
        ));
    }

    return copy;
}


//
//  Uncolor_Array: C
//
void Uncolor_Array(const Array* a)
{
    if (Is_Stub_White(a))
        return; // avoid loop

    Flip_Stub_To_White(a);

    const Element* tail = Array_Tail(a);
    const Element* v = Array_Head(a);
    for (; v != tail; ++v) {
        if (Is_Path(v) or Any_List(v) or Is_Map(v) or Any_Context(v))
            Uncolor(v);
    }
}


//
//  Uncolor: C
//
// Clear the recursion markers for Flex and Object trees.
//
void Uncolor(const Stable* v)
{
    if (Is_Antiform(v))
        return;

    if (Any_List(v))
        Uncolor_Array(Cell_Array(v));
    else if (Is_Path(v)) {
        REBLEN len = Sequence_Len(v);
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
        Uncolor_Array(Varlist_Array(Cell_Varlist(v)));
    else {
        // Shouldn't have marked recursively any non-Array Flexes (no need)
        //
        assert(
            not Any_Series(v)
            or Is_Stub_White(Cell_Flex(v))
        );
    }
}
