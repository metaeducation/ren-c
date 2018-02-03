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
REBARR *Copy_Array_At_Extra_Shallow(
    REBARR *original,
    REBCNT index,
    REBSPC *specifier,
    REBCNT extra,
    REBUPT flags
){
    REBCNT len = ARR_LEN(original);

    if (index > len)
        return Make_Array_For_Copy(extra, flags, original);

    len -= index;

    REBARR *copy = Make_Array_For_Copy(len + extra, flags, original);

    RELVAL *src = ARR_AT(original, index);
    REBVAL *dest = KNOWN(ARR_HEAD(copy));
    REBCNT count = 0;
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
    REBARR *original,
    REBCNT index,
    REBSPC *specifier,
    REBCNT max
){
    const REBFLGS flags = 0;

    if (index > ARR_LEN(original))
        return Make_Array_For_Copy(0, flags, original);

    if (index + max > ARR_LEN(original))
        max = ARR_LEN(original) - index;

    REBARR *copy = Make_Array_For_Copy(max + 1, flags, original);

    REBCNT count = 0;
    const RELVAL *src = ARR_AT(original, index);
    RELVAL *dest = ARR_HEAD(copy);
    for (; count < max; ++count, ++src, ++dest)
        Derelativize(dest, src, specifier);

    TERM_ARRAY_LEN(copy, max);

    return copy;
}


//
//  Copy_Values_Len_Extra_Skip_Shallow_Core: C
//
// Shallow copy the first 'len' values of `head` into a new
// series created to hold exactly that many entries.
//
REBARR *Copy_Values_Len_Extra_Skip_Shallow_Core(
    const RELVAL head[],
    REBSPC *specifier,
    REBCNT len,
    REBCNT extra,
    REBINT skip,
    REBUPT flags
) {
    REBARR *array = Make_Array_Core(len + extra + 1, flags);

    REBCNT count = 0;
    const RELVAL *src = head;
    RELVAL *dest = ARR_HEAD(array);
    for (; count < len; ++count, src += skip, ++dest) {
        Derelativize(dest, src, specifier);
        if (flags & ARRAY_FLAG_VOIDS_LEGAL) {
            if (GET_VAL_FLAG(src, VALUE_FLAG_EVAL_FLIP))
                SET_VAL_FLAG(dest, VALUE_FLAG_EVAL_FLIP);
        }
    }

    TERM_ARRAY_LEN(array, len);

    return array;
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
    RELVAL head[],
    REBSPC *specifier,
    REBCNT len,
    REBFLGS flags,
    REBU64 types
) {
    if (C_STACK_OVERFLOWING(&len))
        Fail_Stack_Overflow();

    RELVAL *v = head;

    REBCNT index;
    for (index = 0; index < len; ++index, ++v) {
        //
        // By the rules, if we need to do a deep copy on the source
        // series then the values inside it must have already been
        // marked managed (because they *might* delve another level deep)
        //
        ASSERT_VALUE_MANAGED(v);

        if (types & FLAGIT_KIND(VAL_TYPE(v)) & TS_SERIES_OBJ) {
            //
            // Objects and series get shallow copied at minimum
            //
            REBSER *series;
            if (ANY_CONTEXT(v)) {
                assert(!IS_FRAME(v)); // !!! Don't exist yet...
                v->payload.any_context.varlist =
                    CTX_VARLIST(Copy_Context_Shallow(VAL_CONTEXT(v)));
                series = SER(CTX_VARLIST(VAL_CONTEXT(v)));
            }
            else {
                if (GET_SER_FLAG(VAL_SERIES(v), SERIES_FLAG_ARRAY)) {
                    REBSPC *derived = Derive_Specifier(specifier, v);
                    series = SER(
                        Copy_Array_At_Extra_Shallow(
                            VAL_ARRAY(v),
                            0, // !!! what if VAL_INDEX() is nonzero?
                            derived,
                            0,
                            flags
                        )
                    );

                    INIT_VAL_ARRAY(v, ARR(series)); // copies args

                    // If it was relative, then copying with a specifier
                    // means it isn't relative any more.
                    //
                    INIT_BINDING(v, UNBOUND);
                }
                else {
                    series = Copy_Sequence(VAL_SERIES(v));
                    INIT_VAL_SERIES(v, series);
                }
            }

            MANAGE_SERIES(series);

            // If we're going to copy deeply, we go back over the shallow
            // copied series and "clonify" the values in it.
            //
            // Since we had to get rid of the relative bindings in the
            // shallow copy, we can pass in SPECIFIED here...but the recursion
            // in Clonify_Values will be threading through any updated
            // specificity through to the new values.
            //
            if (types & FLAGIT_KIND(VAL_TYPE(v)) & TS_ARRAYS_OBJ) {
                REBSPC *derived = Derive_Specifier(specifier, v);
                Clonify_Values_Len_Managed(
                     ARR_HEAD(ARR(series)),
                     derived,
                     VAL_LEN_HEAD(v),
                     flags,
                     types
                );
            }
        }
        else if (
            types & FLAGIT_KIND(VAL_TYPE(v)) & FLAGIT_KIND(REB_FUNCTION)
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
            assert(FALSE);
        }
        else {
            // The value is not on our radar as needing to be processed,
            // so leave it as-is.
        }

        // Value shouldn't be relative after the above processing.
        //
        assert(!IS_RELATIVE(v));
    }
}


//
//  Copy_Array_Core_Managed_Inner_Loop: C
//
//
static REBARR *Copy_Array_Core_Managed_Inner_Loop(
    REBARR *original,
    REBCNT index,
    REBSPC *specifier,
    REBCNT tail,
    REBCNT extra, // currently no one uses--would it also apply deep (?)
    REBFLGS flags,
    REBU64 types
){
    assert(index <= tail && tail <= ARR_LEN(original));

    REBCNT len = tail - index;

    // Currently we start by making a shallow copy and then adjust it

    REBARR *copy = Make_Array_For_Copy(len + extra, flags, original);

    RELVAL *src = ARR_AT(original, index);
    REBVAL *dest = KNOWN(ARR_HEAD(copy));
    REBCNT count = 0;
    for (; count < len; ++count, ++dest, ++src)
        Derelativize(dest, src, specifier);

    TERM_ARRAY_LEN(copy, len);

    MANAGE_ARRAY(copy);

    if (types != 0)
        Clonify_Values_Len_Managed(
            ARR_HEAD(copy), SPECIFIED, ARR_LEN(copy), flags, types
        );

    ASSERT_NO_RELATIVE(copy, types);
    return copy;
}


//
//  Copy_Array_Core_Managed: C
//
// Copy a block, copy specified values, deeply if indicated.
//
// To avoid having to do a second deep walk to add managed bits on all series,
// the resulting array will already be deeply under GC management, and hence
// cannot be freed with Free_Series().
//
REBARR *Copy_Array_Core_Managed(
    REBARR *original,
    REBCNT index,
    REBSPC *specifier,
    REBCNT tail,
    REBCNT extra,
    REBFLGS flags,
    REBU64 types
){
    if (index > tail) // !!! should this be asserted?
        index = tail;

    if (index > ARR_LEN(original)) { // should this be asserted?
        REBARR *copy = Make_Array_Core(extra, flags);
        MANAGE_ARRAY(copy);
        return copy;
    }

    REBARR *copy = Copy_Array_Core_Managed_Inner_Loop(
        original,
        index,
        specifier,
        tail,
        extra,
        flags,
        types
    );

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
    REBARR *original,
    REBFUN *before, // references to `before` will be changed to `after`
    REBFUN *after
){
    const REBFLGS flags = 0;

    REBARR *copy = Make_Array_For_Copy(ARR_LEN(original), flags, original);
    RELVAL *src = ARR_HEAD(original);
    RELVAL *dest = ARR_HEAD(copy);

    for (; NOT_END(src); ++src, ++dest) {
        if (NOT(IS_RELATIVE(src))) {
            Move_Value(dest, KNOWN(src));
            continue;
        }

        // All relative values under a sub-block must be relative to the
        // same function.
        //
        assert(VAL_RELATIVE(src) == before);

        Move_Value_Header(dest, src);

        if (ANY_ARRAY(src)) {
            dest->payload.any_series.series = SER(
                Copy_Rerelativized_Array_Deep_Managed(
                    VAL_ARRAY(src), before, after
                )
            );
            dest->payload.any_series.index = src->payload.any_series.index;
            INIT_BINDING(dest, after); // relative binding
        }
        else {
            assert(ANY_WORD(src));
            dest->payload.any_word = src->payload.any_word;
            INIT_BINDING(dest, after);
        }

    }

    TERM_ARRAY_LEN(copy, ARR_LEN(original));
    MANAGE_ARRAY(copy);

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
REBVAL *Alloc_Tail_Array(REBARR *a)
{
    EXPAND_SERIES_TAIL(SER(a), 1);
    TERM_ARRAY_LEN(a, ARR_LEN(a));
    return SINK(ARR_LAST(a));
}


//
//  Uncolor_Array: C
//
void Uncolor_Array(REBARR *a)
{
    if (Is_Series_White(SER(a)))
        return; // avoid loop

    Flip_Series_To_White(SER(a));

    RELVAL *val;
    for (val = ARR_HEAD(a); NOT_END(val); ++val)
        if (ANY_ARRAY(val) || ANY_CONTEXT(val))
            Uncolor(val);
}


//
//  Uncolor: C
//
// Clear the recusion markers for series and object trees.
//
void Uncolor(RELVAL *val)
{
    REBARR *array;

    if (ANY_ARRAY(val))
        array = VAL_ARRAY(val);
    else if (ANY_CONTEXT(val))
        array = CTX_VARLIST(VAL_CONTEXT(val));
    else {
        // Shouldn't have marked recursively any non-array series (no need)
        //
        assert(
            !ANY_SERIES(val)
            || Is_Series_White(VAL_SERIES(val))
        );
        return;
    }

    Uncolor_Array(array);
}
