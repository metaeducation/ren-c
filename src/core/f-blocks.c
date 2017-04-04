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


// !!! Currently, callers don't specify if they are copying an array to turn
// it into a paramlist or varlist, or to use as the kind of array the user
// might see.  If we used plain Make_Array() then it would add a flag saying
// there were line numbers available, which might compete with flags written
// later.  Pass SERIES_FLAG_ARRAY because it only will trigger the line
// number behavior if the flags are 0.
//
#define Make_Array_For_Copy(a) \
    Make_Array_Core((a), 0)


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
    REBCNT extra
) {
    REBCNT len = ARR_LEN(original);

    if (index > len)
        return Make_Array_For_Copy(0);

    len -= index;

    REBARR *copy = Make_Array_For_Copy(len + extra + 1);

    if (specifier == SPECIFIED) {
        //
        // We can just bit-copy a fully specified array.  By its definition
        // it may not contain any RELVALs.  But in the debug build, double
        // check that...
        //
    #if !defined(NDEBUG)
        RELVAL *check = ARR_AT(original, index);
        REBCNT count = 0;
        for (; count < len; ++count)
            assert(IS_SPECIFIC(check));
    #endif

        memcpy(ARR_HEAD(copy), ARR_AT(original, index), len * sizeof(REBVAL));
    }
    else {
        // Any RELVALs will have to be handled.  Review if a memcpy with
        // a touch-up phase is faster, or if there is any less naive way.
        //
        RELVAL *src = ARR_AT(original, index);
        REBVAL *dest = KNOWN(ARR_HEAD(copy));
        REBCNT count = 0;
        for (; count < len; ++count, ++dest, ++src)
            Derelativize(dest, src, specifier);
    }

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
) {
    if (index > ARR_LEN(original))
        return Make_Array_For_Copy(0);

    if (index + max > ARR_LEN(original))
        max = ARR_LEN(original) - index;

    REBARR *copy = Make_Array_For_Copy(max + 1);

    if (specifier == SPECIFIED) {
    #if !defined(NDEBUG)
        REBCNT count = 0;
        const RELVAL *check = ARR_AT(original, index);
        for (; count < max; ++count, ++check) {
            assert(IS_SPECIFIC(check));
        }
    #endif
        memcpy(ARR_HEAD(copy), ARR_AT(original, index), max * sizeof(REBVAL));
    }
    else {
        REBCNT count = 0;
        const RELVAL *src = ARR_AT(original, index);
        RELVAL *dest = ARR_HEAD(copy);
        for (; count < max; ++count, ++src, ++dest)
            Derelativize(dest, src, specifier);
    }

    TERM_ARRAY_LEN(copy, max);

    return copy;
}


//
//  Copy_Values_Len_Extra_Skip_Shallow: C
//
// Shallow copy the first 'len' values of `head` into a new
// series created to hold exactly that many entries.
//
REBARR *Copy_Values_Len_Extra_Skip_Shallow_Core(
    const RELVAL *head,
    REBSPC *specifier,
    REBCNT len,
    REBCNT extra,
    REBINT skip,
    REBUPT flags
) {
    REBARR *array = Make_Array_Core(len + extra + 1, flags);

    if (specifier == SPECIFIED && skip == 1) {
    #if !defined(NDEBUG)
        REBCNT count = 0;
        const RELVAL *check = head;
        for (; count < len; ++count, ++check) {
            assert(IS_SPECIFIC(check));
        }
    #endif
        memcpy(ARR_HEAD(array), head, len * sizeof(REBVAL));
    }
    else {
        REBCNT count = 0;
        const RELVAL *src = head;
        RELVAL *dest = ARR_HEAD(array);
        for (; count < len; ++count, src += skip, ++dest)
            Derelativize(dest, src, specifier);
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
    RELVAL *head,
    REBSPC *specifier,
    REBCNT len,
    REBOOL deep,
    REBU64 types
) {
    if (C_STACK_OVERFLOWING(&len)) Trap_Stack_Overflow();

    RELVAL *value = head;

    REBCNT index;
    for (index = 0; index < len; index++, value++) {
        //
        // By the rules, if we need to do a deep copy on the source
        // series then the values inside it must have already been
        // marked managed (because they *might* delve another level deep)
        //
        ASSERT_VALUE_MANAGED(value);

        if (types & FLAGIT_KIND(VAL_TYPE(value)) & TS_SERIES_OBJ) {
        #if !defined(NDEBUG)
            REBOOL legacy = FALSE;
        #endif

            // Objects and series get shallow copied at minimum
            //
            REBSER *series;
            if (ANY_CONTEXT(value)) {
            #if !defined(NDEBUG)
                legacy = Get_Ser_Info(
                    CTX_VARLIST(VAL_CONTEXT(value)),
                    SERIES_INFO_LEGACY_DEBUG
                );
            #endif

                assert(!IS_FRAME(value)); // !!! Don't exist yet...
                value->payload.any_context.varlist =
                    CTX_VARLIST(Copy_Context_Shallow(VAL_CONTEXT(value)));
                series = AS_SERIES(CTX_VARLIST(VAL_CONTEXT(value)));
            }
            else {
                if (Get_Ser_Flag(VAL_SERIES(value), SERIES_FLAG_ARRAY)) {
                #if !defined(NDEBUG)
                    legacy = Get_Ser_Info(
                        VAL_ARRAY(value), SERIES_INFO_LEGACY_DEBUG
                    );
                #endif

                    REBSPC *derived = Derive_Specifier(specifier, value);
                    series = AS_SERIES(
                        Copy_Array_Shallow(
                            VAL_ARRAY(value),
                            derived
                        )
                    );

                    INIT_VAL_ARRAY(value, AS_ARRAY(series)); // copies args

                    // If it was relative, then copying with a specifier
                    // means it isn't relative any more.
                    //
                    INIT_SPECIFIC(value, SPECIFIED);
                }
                else {
                    series = Copy_Sequence(VAL_SERIES(value));
                    INIT_VAL_SERIES(value, series);
                }
            }

        #if !defined(NDEBUG)
            if (legacy) // propagate legacy
                Set_Ser_Info(series, SERIES_INFO_LEGACY_DEBUG);
        #endif

            MANAGE_SERIES(series);

            if (!deep) continue;

            // If we're going to copy deeply, we go back over the shallow
            // copied series and "clonify" the values in it.
            //
            // Since we had to get rid of the relative bindings in the
            // shallow copy, we can pass in SPECIFIED here...but the recursion
            // in Clonify_Values will be threading through any updated specificity
            // through to the new values.
            //
            if (types & FLAGIT_KIND(VAL_TYPE(value)) & TS_ARRAYS_OBJ) {
                REBSPC *derived = Derive_Specifier(specifier, value);
                Clonify_Values_Len_Managed(
                     ARR_HEAD(AS_ARRAY(series)),
                     derived,
                     VAL_LEN_HEAD(value),
                     deep,
                     types
                );
            }
        }
        else if (
            types & FLAGIT_KIND(VAL_TYPE(value)) & FLAGIT_KIND(REB_FUNCTION)
        ) {
            Clonify_Function(KNOWN(value)); // functions never "relative"
        }
        else {
            // The value is not on our radar as needing to be processed,
            // so leave it as-is.
        }

        // Value shouldn't be relative after the above processing.
        //
        assert(!IS_RELATIVE(value));
    }
}


//
//  Copy_Array_Core_Managed: C
//
// Copy a block, copy specified values, deeply if indicated.
//
// The resulting series will already be under GC management,
// and hence cannot be freed with Free_Series().
//
REBARR *Copy_Array_Core_Managed(
    REBARR *original,
    REBCNT index,
    REBSPC *specifier,
    REBCNT tail,
    REBCNT extra,
    REBOOL deep,
    REBU64 types
) {
    REBARR *copy;

    if (index > tail) index = tail;

    if (index > ARR_LEN(original)) {
        copy = Make_Array_For_Copy(extra);
        MANAGE_ARRAY(copy);
    }
    else {
        copy = Copy_Values_Len_Extra_Shallow(
            ARR_AT(original, index), specifier, tail - index, extra
        );
        MANAGE_ARRAY(copy);

        if (types != 0) // the copy above should have specified top level
            Clonify_Values_Len_Managed(
                ARR_HEAD(copy), SPECIFIED, ARR_LEN(copy), deep, types
            );
    }

#if !defined(NDEBUG)
    //
    // Propagate legacy flag, hence if a legacy array was loaded with
    // `[switch 1 [2]]` in it (for instance) then when that code is used to
    // make a function body, the `[switch 1 [2]]` in that body will also
    // be marked legacy.  Then if it runs, the SWITCH can dispatch to return
    // blank instead of the Ren-C behavior of returning `2`.
    //
    if (Get_Ser_Info(original, SERIES_INFO_LEGACY_DEBUG))
        Set_Ser_Info(copy, SERIES_INFO_LEGACY_DEBUG);
#endif

    ASSERT_NO_RELATIVE(copy, deep);
    return copy;
}


//
//  Copy_Array_At_Extra_Deep_Managed: C
//
// Deep copy an array, including all series (strings, blocks,
// parens, objects...) excluding images, bitsets, maps, etc.
// The set of exclusions is the typeset TS_NOT_COPIED.
//
// The resulting array will already be under GC management,
// and hence cannot be freed with Free_Series().
//
// Note: If this were declared as a macro it would use the
// `array` parameter more than once, and have to be in all-caps
// to warn against usage with arguments that have side-effects.
//
REBARR *Copy_Array_At_Extra_Deep_Managed(
    REBARR *original,
    REBCNT index,
    REBSPC *specifier,
    REBCNT extra
) {
    REBARR *copy = Copy_Array_Core_Managed(
        original,
        index, // at
        specifier,
        ARR_LEN(original), // tail
        extra, // extra
        TRUE, // deep
        TS_SERIES & ~TS_NOT_COPIED // types
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
) {
    REBARR *copy = Make_Array_For_Copy(ARR_LEN(original));
    RELVAL *src = ARR_HEAD(original);
    RELVAL *dest = ARR_HEAD(copy);

    for (; NOT_END(src); ++src, ++dest) {
        if (!IS_RELATIVE(src)) {
            *dest = *src;
            continue;
        }

        assert(VAL_RELATIVE(src) == before);
        if (ANY_ARRAY(src)) {
            *dest = *src; // !!! could copy just fields not overwritten
            dest->payload.any_series.series = AS_SERIES(
                Copy_Rerelativized_Array_Deep_Managed(
                    VAL_ARRAY(src), before, after
                )
            );
            INIT_RELATIVE(dest, after);
        }
        else {
            assert(ANY_WORD(src));
            *dest = *src; // !!! could copy just fields not overwritten
            INIT_WORD_FUNC(dest, after);
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
    EXPAND_SERIES_TAIL(AS_SERIES(a), 1);
    TERM_ARRAY_LEN(a, ARR_LEN(a));
    return SINK(ARR_LAST(a));
}


//
//  Find_Same_Array: C
//
// Scan a block for any values that reference blocks related
// to the value provided.
//
// !!! This was used for detection of cycles during MOLD.  The idea is that
// while it is outputting a series, it doesn't want to see that series
// again.  For the moment the only places to worry about with that are
// context varlists and block series or maps.  (Though a function contains
// series for the spec, body, and paramlist...the spec and body are blocks,
// and so recursion would be found when the blocks were output.)
//
REBCNT Find_Same_Array(REBARR *search_values, const RELVAL *value)
{
    REBCNT index = 0;
    REBARR *array;
    RELVAL *other;

    if (ANY_ARRAY(value))
        array = VAL_ARRAY(value);
    else if (IS_MAP(value))
        array = MAP_PAIRLIST(VAL_MAP(value));
    else if (ANY_CONTEXT(value))
        array = CTX_VARLIST(VAL_CONTEXT(value));
    else {
        // Value being worked with is not a candidate for containing an
        // array that could form a loop with one of the search_list values
        //
        return NOT_FOUND;
    }

    other = ARR_HEAD(search_values);
    for (; NOT_END(other); other++, index++) {
        if (ANY_ARRAY(other)) {
            if (array == VAL_ARRAY(other))
                return index;
        }
        else if (IS_MAP(other)) {
            if (array == MAP_PAIRLIST(VAL_MAP(other)))
                return index;
        }
        else if (ANY_CONTEXT(other)) {
            if (array == CTX_VARLIST(VAL_CONTEXT(other)))
                return index;
        }
    }

    return NOT_FOUND;
}


//
//  Uncolor_Array: C
//
void Uncolor_Array(REBARR *a)
{
    if (Is_Series_White(AS_SERIES(a)))
        return; // avoid loop

    Flip_Series_To_White(AS_SERIES(a));

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
