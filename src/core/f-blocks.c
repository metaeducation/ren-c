/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2012 REBOL Technologies
**  REBOL is a trademark of REBOL Technologies
**
**  Licensed under the Apache License, Version 2.0 (the "License");
**  you may not use this file except in compliance with the License.
**  You may obtain a copy of the License at
**
**  http://www.apache.org/licenses/LICENSE-2.0
**
**  Unless required by applicable law or agreed to in writing, software
**  distributed under the License is distributed on an "AS IS" BASIS,
**  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**  See the License for the specific language governing permissions and
**  limitations under the License.
**
************************************************************************
**
**  Module:  f-blocks.c
**  Summary: primary block series support functions
**  Section: functional
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

#include "sys-core.h"


//
//  Make_Array: C
// 
// Make a series that is the right size to store REBVALs (and
// marked for the garbage collector to look into recursively).
// Terminator included implicitly. Sets TAIL to zero.
//
REBSER *Make_Array(REBCNT capacity)
{
    REBSER *series = Make_Series(capacity + 1, sizeof(REBVAL), MKS_ARRAY);
    SET_END(BLK_HEAD(series));

    return series;
}


//
//  Copy_Array_At_Extra_Shallow: C
// 
// Shallow copy an array from the given index thru the tail.
// Additional capacity beyond what is required can be added
// by giving an `extra` count of how many value cells one needs.
//
REBSER *Copy_Array_At_Extra_Shallow(REBSER *array, REBCNT index, REBCNT extra)
{
    REBCNT len = SERIES_TAIL(array);
    REBSER *series;

    if (index > len) return Make_Array(extra);

    len -= index;
    series = Make_Series(len + extra + 1, sizeof(REBVAL), MKS_ARRAY);

    memcpy(series->data, BLK_SKIP(array, index), len * sizeof(REBVAL));
    SERIES_TAIL(series) = len;
    TERM_ARRAY(series);

    return series;
}


//
//  Copy_Array_At_Max_Shallow: C
// 
// Shallow copy an array from the given index for given maximum
// length (clipping if it exceeds the array length)
//
REBSER *Copy_Array_At_Max_Shallow(REBSER *array, REBCNT index, REBCNT max)
{
    REBSER *series;

    if (index > SERIES_TAIL(array)) return Make_Array(0);
    if (index + max > SERIES_TAIL(array)) max = SERIES_TAIL(array) - index;

    series = Make_Series(max + 1, sizeof(REBVAL), MKS_ARRAY);

    memcpy(series->data, BLK_SKIP(array, index), max * sizeof(REBVAL));
    SERIES_TAIL(series) = max;
    TERM_ARRAY(series);

    return series;
}


//
//  Copy_Values_Len_Shallow_Extra: C
// 
// Shallow copy the first 'len' values of `value[]` into a new
// series created to hold exactly that many entries.
//
REBSER *Copy_Values_Len_Shallow_Extra(REBVAL value[], REBCNT len, REBCNT extra)
{
    REBSER *series;

    series = Make_Series(len + extra + 1, sizeof(REBVAL), MKS_ARRAY);

    memcpy(series->data, &value[0], len * sizeof(REBVAL));
    SERIES_TAIL(series) = len;
    TERM_ARRAY(series);

    return series;
}


//
//  Clonify_Values_Len_Managed: C
// 
// Update the first `len` elements of value[] to clone the series
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
void Clonify_Values_Len_Managed(REBVAL value[], REBCNT len, REBOOL deep, REBU64 types)
{
    REBCNT index;

    if (C_STACK_OVERFLOWING(&len)) Trap_Stack_Overflow();

    for (index = 0; index < len; index++, value++) {
        // By the rules, if we need to do a deep copy on the source
        // series then the values inside it must have already been
        // marked managed (because they *might* delve another level deep)
        ASSERT_VALUE_MANAGED(value);

        if (types & FLAGIT_64(VAL_TYPE(value)) & TS_SERIES_OBJ) {
            //
            // Objects and series get shallow copied at minimum
            //
            REBSER *series;
            if (ANY_CONTEXT(value)) {
                VAL_FRAME(value) = Copy_Frame_Shallow_Managed(
                    VAL_FRAME(value)
                );
                series = FRAME_VARLIST(VAL_FRAME(value));
            }
            else {
                if (Is_Array_Series(VAL_SERIES(value)))
                    series = Copy_Array_Shallow(VAL_SERIES(value));
                else
                    series = Copy_Sequence(VAL_SERIES(value));
                VAL_SERIES(value) = series;
            }

            MANAGE_SERIES(series);

            if (!deep) continue;

            // If we're going to copy deeply, we go back over the shallow
            // copied series and "clonify" the values in it.
            //
            if (types & FLAGIT_64(VAL_TYPE(value)) & TS_ARRAYS_OBJ) {
                Clonify_Values_Len_Managed(
                     BLK_HEAD(series), VAL_TAIL(value), deep, types
                );
            }
        }
        else if (types & FLAGIT_64(VAL_TYPE(value)) & TS_FUNCLOS) {
            Clonify_Function(value);
        }
        else {
            // The value is not on our radar as needing to be processed,
            // so leave it as-is.
        }
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
REBSER *Copy_Array_Core_Managed(
    REBSER *block,
    REBCNT index,
    REBCNT tail,
    REBCNT extra,
    REBOOL deep,
    REBU64 types
) {
    REBSER *series;

    assert(Is_Array_Series(block));

    if (index > tail) index = tail;

    if (index > SERIES_TAIL(block)) {
        series = Make_Array(extra);
        MANAGE_SERIES(series);
    }
    else {
        series = Copy_Values_Len_Shallow_Extra(
            BLK_SKIP(block, index), tail - index, extra
        );
        MANAGE_SERIES(series);

        if (types != 0)
            Clonify_Values_Len_Managed(
                BLK_HEAD(series), SERIES_TAIL(series), deep, types
            );
    }

    return series;
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
REBSER *Copy_Array_At_Extra_Deep_Managed(
    REBSER *array,
    REBCNT index,
    REBCNT extra
) {
    return Copy_Array_Core_Managed(
        array,
        index, // at
        SERIES_TAIL(array), // tail
        extra, // extra
        TRUE, // deep
        TS_SERIES & ~TS_NOT_COPIED // types
    );
}


//
//  Copy_Stack_Values: C
// 
// Copy computed values from the stack into the series
// specified by "into", or if into is NULL then store it as a
// block on top of the stack.  (Also checks to see if into
// is protected, and will trigger a trap if that is the case.)
//
void Copy_Stack_Values(REBINT start, REBVAL *into)
{
    // REVIEW: Can we change the interface to not take a REBVAL
    // for into, in order to better show the subtypes allowed here?
    // Currently it can be any-block!, any-string!, or binary!

    REBSER *series;
    REBVAL *blk = DS_AT(start);
    REBCNT len = DSP - start + 1;

    if (into) {
        series = VAL_SERIES(into);

        FAIL_IF_PROTECTED_SERIES(series);

        if (ANY_ARRAY(into)) {
            // When the target is an any-block, we can do an ordinary
            // insertion of the values via a memcpy()-style operation

            VAL_INDEX(into) = Insert_Series(
                series, VAL_INDEX(into), cast(REBYTE*, blk), len
            );

            DS_DROP_TO(start);

            Val_Init_Series_Index(
                DS_TOP, VAL_TYPE(into), series, VAL_INDEX(into)
            );
        }
        else {
            // When the target is a string or binary series, we defer
            // to the same code used by A_INSERT.  Because the interface
            // does not take a memory address and count, we insert
            // the values one by one.

            // REVIEW: Is there a way to do this without the loop,
            // which may be able to make a better guess of how much
            // to expand the target series by based on the size of
            // the operation?

            REBCNT i;
            REBCNT flags = 0;
            // you get weird behavior if you don't do this
            if (IS_BINARY(into)) SET_FLAG(flags, AN_SERIES);
            for (i = 0; i < len; i++) {
                VAL_INDEX(into) += Modify_String(
                    A_INSERT,
                    VAL_SERIES(into),
                    VAL_INDEX(into) + i,
                    blk + i,
                    flags,
                    1, // insert one element at a time
                    1 // duplication count
                );
            }

            DS_DROP_TO(start);

            // We want index of result just past the last element we inserted
            Val_Init_Series_Index(
                DS_TOP, VAL_TYPE(into), series, VAL_INDEX(into)
            );
        }
    }
    else {
        series = Make_Series(len + 1, sizeof(REBVAL), MKS_ARRAY);

        memcpy(series->data, blk, len * sizeof(REBVAL));
        SERIES_TAIL(series) = len;
        TERM_ARRAY(series);

        DS_DROP_TO(start);
        Val_Init_Series_Index(DS_TOP, REB_BLOCK, series, 0);
    }
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
REBVAL *Alloc_Tail_Array(REBSER *block)
{
    REBVAL *tail;

    EXPAND_SERIES_TAIL(block, 1);
    tail = BLK_TAIL(block);
    SET_END(tail);

    SET_TRASH_IF_DEBUG(tail - 1); // No-op in release builds
    return tail - 1;
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
// context frames and block series or maps.  (Though a function contains
// series for the spec, body, and paramlist...the spec and body are blocks,
// and so recursion would be found when the blocks were output.)
//
REBCNT Find_Same_Array(REBSER *search_values, const REBVAL *value)
{
    REBCNT index = 0;
    REBSER *array;
    REBVAL *other;

    assert(Is_Array_Series(search_values));

    if (ANY_ARRAY(value) || IS_MAP(value))
        array = VAL_SERIES(value);
    else if (ANY_CONTEXT(value))
        array = FRAME_VARLIST(VAL_FRAME(value));
    else {
        // Value being worked with is not a candidate for containing an
        // array that could form a loop with one of the search_list values
        //
        return NOT_FOUND;
    }

    for (other = BLK_HEAD(search_values); NOT_END(other); other++, index++) {
        if (ANY_ARRAY(other) || IS_MAP(other)) {
            if (array == VAL_SERIES(other))
                return index;
        }
        else if (ANY_CONTEXT(other)) {
            if (array == FRAME_VARLIST(VAL_FRAME(other)))
                return index;
        }
    }

    return NOT_FOUND;
}


//
//  Unmark: C
// 
// Clear the recusion markers for series and object trees.
// 
// Note: these markers are also used for GC. Functions that
// call this must not be able to trigger GC!
//
void Unmark(REBVAL *val)
{
    REBSER *series;
    if (ANY_SERIES(val))
        series = VAL_SERIES(val);
    else if (ANY_CONTEXT(val))
        series = FRAME_VARLIST(VAL_FRAME(val));
    else
        return;

    if (!SERIES_GET_FLAG(series, SER_MARK)) return; // avoid loop

    SERIES_CLR_FLAG(series, SER_MARK);

    for (val = VAL_BLK_HEAD(val); NOT_END(val); val++)
        Unmark(val);
}
