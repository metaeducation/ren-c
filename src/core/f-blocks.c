//
//  file: %f-blocks.c
//  summary: "primary block series support functions"
//  section: functional
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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


//
//  Copy_Array_At_Extra_Shallow: C
//
// Shallow copy an array from the given index thru the tail.
// Additional capacity beyond what is required can be added
// by giving an `extra` count of how many value cells one needs.
//
Array* Copy_Array_At_Extra_Shallow(
    Array* original,
    REBLEN index,
    Specifier* specifier,
    REBLEN extra,
    Flags flags
){
    REBLEN len = Array_Len(original);

    if (index > len)
        return Make_Arr_For_Copy(extra, flags, original);

    len -= index;

    Array* copy = Make_Arr_For_Copy(len + extra, flags, original);

    Cell* src = Array_At(original, index);
    Cell* dest = Array_Head(copy);
    REBLEN count = 0;
    for (; count < len; ++count, ++dest, ++src)
        Derelativize(dest, src, specifier);

    Term_Array_Len(copy, len);

    return copy;
}


//
//  Copy_Array_At_Max_Shallow: C
//
// Shallow copy an array from the given index for given maximum
// length (clipping if it exceeds the array length)
//
Array* Copy_Array_At_Max_Shallow(
    Array* original,
    REBLEN index,
    Specifier* specifier,
    REBLEN max
){
    const Flags flags = 0;

    if (index > Array_Len(original))
        return Make_Arr_For_Copy(0, flags, original);

    if (index + max > Array_Len(original))
        max = Array_Len(original) - index;

    Array* copy = Make_Arr_For_Copy(max, flags, original);

    REBLEN count = 0;
    const Cell* src = Array_At(original, index);
    Cell* dest = Array_Head(copy);
    for (; count < max; ++count, ++src, ++dest)
        Derelativize(dest, src, specifier);

    Term_Array_Len(copy, max);

    return copy;
}


//
//  Copy_Values_Len_Extra_Shallow_Core: C
//
// Shallow copy the first 'len' values of `head` into a new
// series created to hold exactly that many entries.
//
Array* Copy_Values_Len_Extra_Shallow_Core(
    const Cell* head,
    Specifier* specifier,
    REBLEN len,
    REBLEN extra,
    Flags flags
){
    Array* a = Make_Array_Core(len + extra, flags);

    REBLEN count = 0;
    const Cell* src = head;
    Cell* dest = Array_Head(a);
    for (; count < len; ++count, ++src, ++dest) {
        Derelativize(dest, src, specifier);
        if (not (flags & ARRAY_FLAG_ANTIFORMS_LEGAL))
            assert(not Is_Antiform(dest));
    }

    Term_Array_Len(a, len);
    return a;
}


//
//  Clonify_Values_Len_Managed: C
//
// Update the first `len` elements of `head[]` to clone the series
// embedded in them *if* they are in the given set of types (and
// if "cloning" makes sense for them, e.g. they are not simple
// scalars).  If the `deep` flag is set, recurse into subseries
// and objects when that type is matched for clonifying.
//
// Note: The resulting clones will be managed.  The model for
// lists only allows the topmost level to contain unmanaged
// values...and we *assume* the values we are operating on here
// live inside of an array.  (We also assume the source values
// are in an array, and assert that they are managed.)
//
void Clonify_Values_Len_Managed(
    Cell* head,
    Specifier* specifier,
    REBLEN len,
    REBU64 types
) {
    if (C_STACK_OVERFLOWING(&len))
        Panic_Stack_Overflow();

    Cell* v = head;

    REBLEN index;
    for (index = 0; index < len; ++index, ++v) {
        if (types & FLAG_TYPE(Type_Of(v)) & TS_SERIES_OBJ) {
            //
            // Objects and series get shallow copied at minimum
            //
            Flex* series;
            if (Any_Context(v)) {
                v->payload.any_context.varlist = Varlist_Array(
                    Copy_Context_Shallow_Managed(Cell_Varlist(v))
                );
                series = Varlist_Array(Cell_Varlist(v));
            }
            else {
                if (Is_Flex_Array(Cell_Flex(v))) {
                    Specifier* derived = Derive_Specifier(specifier, v);
                    series = Copy_Array_At_Extra_Shallow(
                        Cell_Array(v),
                        0, // !!! what if VAL_INDEX() is nonzero?
                        derived,
                        0,
                        NODE_FLAG_MANAGED
                    );

                    INIT_VAL_ARRAY(v, cast_Array(series)); // copies args

                    // If it was relative, then copying with a specifier
                    // means it isn't relative any more.
                    //
                    INIT_BINDING(v, UNBOUND);
                }
                else {
                    series = Copy_Non_Array_Flex_Core(
                        Cell_Flex(v),
                        NODE_FLAG_MANAGED
                    );
                    Set_Cell_Flex(v, series);
                }
            }

            // If we're going to copy deeply, we go back over the shallow
            // copied series and "clonify" the values in it.
            //
            // Since we had to get rid of the relative bindings in the
            // shallow copy, we can pass in SPECIFIED here...but the recursion
            // in Clonify_Values will be threading through any updated
            // specificity through to the new values.
            //
            if (types & FLAG_TYPE(Type_Of(v)) & TS_LISTS_OBJ) {
                Specifier* derived = Derive_Specifier(specifier, v);
                Clonify_Values_Len_Managed(
                     Array_Head(cast_Array(series)),
                     derived,
                     VAL_LEN_HEAD(v),
                     types
                );
            }
        }
        else if (
            types & FLAG_TYPE(Type_Of(v)) & FLAG_TYPE(TYPE_ACTION)
        ){
            // !!! While Ren-C has abandoned the concept of copying the body
            // of functions (they are black boxes which may not *have* a
            // body), it would still theoretically be possible to do what
            // COPY does and make a function with a new and independently
            // hijackable identity.  Assume for now it's better that the
            // HIJACK of a method for one object will hijack it for all
            // objects, and one must filter in the hijacking's body if one
            // wants to take more specific action.
            //
            assert(false);
        }
        else {
            // The value is not on our radar as needing to be processed,
            // so leave it as-is.
        }

        // Value shouldn't be relative after the above processing.
        //
        assert(not IS_RELATIVE(v));
    }
}


//
//  Copy_Array_Core_Managed_Inner_Loop: C
//
//
static Array* Copy_Array_Core_Managed_Inner_Loop(
    Array* original,
    REBLEN index,
    Specifier* specifier,
    REBLEN tail,
    REBLEN extra, // currently no one uses--would it also apply deep (?)
    Flags flags,
    REBU64 types
){
    assert(index <= tail and tail <= Array_Len(original));
    assert(flags & NODE_FLAG_MANAGED);

    REBLEN len = tail - index;

    // Currently we start by making a shallow copy and then adjust it

    Array* copy = Make_Arr_For_Copy(len + extra, flags, original);

    Cell* src = Array_At(original, index);
    Cell* dest = Array_Head(copy);
    REBLEN count = 0;
    for (; count < len; ++count, ++dest, ++src)
        Derelativize(dest, src, specifier);

    Term_Array_Len(copy, len);

    if (types != 0)
        Clonify_Values_Len_Managed(
            Array_Head(copy), SPECIFIED, Array_Len(copy), types
        );

    return copy;
}


//
//  Copy_Array_Core_Managed: C
//
// Copy a block, copy specified values, deeply if indicated.
//
// To avoid having to do a second deep walk to add managed bits on all series,
// the resulting array will already be deeply under GC management, and hence
// cannot be freed with Free_Unmanaged_Flex().
//
Array* Copy_Array_Core_Managed(
    Array* original,
    REBLEN index,
    Specifier* specifier,
    REBLEN tail,
    REBLEN extra,
    Flags flags,
    REBU64 types
){
    if (index > tail) // !!! should this be asserted?
        index = tail;

    if (index > Array_Len(original)) // !!! should this be asserted?
        return Make_Array_Core(extra, flags | NODE_FLAG_MANAGED);

    return Copy_Array_Core_Managed_Inner_Loop(
        original,
        index,
        specifier,
        tail,
        extra,
        flags | NODE_FLAG_MANAGED,
        types
    );
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
    Array* original,
    REBACT *before, // references to `before` will be changed to `after`
    REBACT *after
){
    const Flags flags = NODE_FLAG_MANAGED;

    Array* copy = Make_Arr_For_Copy(Array_Len(original), flags, original);
    Cell* src = Array_Head(original);
    Cell* dest = Array_Head(copy);

    for (; NOT_END(src); ++src, ++dest) {
        if (not IS_RELATIVE(src)) {
            Copy_Cell(dest, KNOWN(src));
            continue;
        }

        // All relative values under a sub-block must be relative to the
        // same function.
        //
        assert(VAL_RELATIVE(src) == before);

        Copy_Cell_Header(dest, src);

        if (Any_List(src)) {
            dest->payload.any_series.series =
                Copy_Rerelativized_Array_Deep_Managed(
                    Cell_Array(src), before, after
                );
            dest->payload.any_series.index = src->payload.any_series.index;
            INIT_BINDING(dest, after); // relative binding
        }
        else {
            assert(Any_Word(src));
            dest->payload.any_word = src->payload.any_word;
            INIT_BINDING(dest, after);
        }

    }

    Term_Array_Len(copy, Array_Len(original));

    return copy;
}


//
//  Alloc_Tail_Array: C
//
// Append a Cell-size slot to Rebol Array series at its tail.
// Will use existing memory capacity already in the series if it
// is available, but will expand the series if necessary.
// Returns the new value for you to initialize.
//
// Note: Updates the termination and tail.
//
Cell* Alloc_Tail_Array(Array* a)
{
    Expand_Flex_Tail(a, 1);
    Term_Array_Len(a, Array_Len(a));
    Cell* last = Array_Last(a);
    Erase_Cell(last);
    return last;
}


//
//  Uncolor_Array: C
//
void Uncolor_Array(Array* a)
{
    if (Is_Flex_White(a))
        return; // avoid loop

    Flip_Flex_To_White(a);

    Cell* val;
    for (val = Array_Head(a); NOT_END(val); ++val)
        if (Any_List(val) or Is_Map(val) or Any_Context(val))
            Uncolor(val);
}


//
//  Uncolor: C
//
// Clear the recusion markers for series and object trees.
//
void Uncolor(Cell* v)
{
    Array* array;

    if (Any_List(v))
        array = Cell_Array(v);
    else if (Is_Map(v))
        array = MAP_PAIRLIST(VAL_MAP(v));
    else if (Any_Context(v))
        array = Varlist_Array(Cell_Varlist(v));
    else {
        // Shouldn't have marked recursively any non-array series (no need)
        //
        assert(
            not Any_Series(v)
            or Is_Flex_White(Cell_Flex(v))
        );
        return;
    }

    Uncolor_Array(array);
}
