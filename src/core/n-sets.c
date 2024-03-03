//
//  File: %n-sets.c
//  Summary: "native functions for data sets"
//  Section: natives
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
//  Make_Set_Operation_Series: C
//
// Do set operations on a series.  Case-sensitive if `cased` is TRUE.
// `skip` is the record size.
//
REBSER *Make_Set_Operation_Series(
    const Value* val1,
    const Value* val2,
    REBFLGS flags,
    bool cased,
    REBLEN skip
){
    assert(ANY_SERIES(val1));

    if (val2) {
        assert(ANY_SERIES(val2));

        if (ANY_ARRAY(val1)) {
            if (!ANY_ARRAY(val2))
                fail (Error_Unexpected_Type(VAL_TYPE(val1), VAL_TYPE(val2)));

            // As long as they're both arrays, we're willing to do:
            //
            //     >> union the (a b c) 'b/d/e
            //     (a b c d e)
            //
            // The type of the result will match the first value.
        }
        else if (ANY_STRING(val1)) {

            // We will similarly do any two ANY-STRING! types:
            //
            //      >> union <abc> "bde"
            //      <abcde>

            if (not ANY_STRING((val2)))
                fail (Error_Unexpected_Type(VAL_TYPE(val1), VAL_TYPE(val2)));
        }
        else {
            // Binaries only operate with other binaries
            assert(IS_BINARY(val1));
            if (not IS_BINARY(val2))
                fail (Error_Unexpected_Type(VAL_TYPE(val1), VAL_TYPE(val2)));
        }
    }

    // Calculate `i` as maximum length of result block.  The temporary buffer
    // will be allocated at this size, but copied out at the exact size of
    // the actual result.
    //
    REBLEN i = VAL_LEN_AT(val1);
    if (flags & SOP_FLAG_BOTH)
        i += VAL_LEN_AT(val2);

    REBINT h = 1; // used for both logic true/false and hash check
    bool first_pass = true; // are we in the first pass over the series?
    REBSER *out_ser;

    if (ANY_ARRAY(val1)) {
        REBSER *hser = 0;   // hash table for series
        REBSER *hret;       // hash table for return series

        // The buffer used for building the return series.  This creates
        // a new buffer every time, but reusing one might be slightly more
        // efficient.
        //
        REBSER *buffer = SER(Make_Arr(i));
        hret = Make_Hash_Sequence(i);   // allocated

        // Optimization note: !!
        // This code could be optimized for small blocks by not hashing them
        // and extending Find_Key to FIND on the value itself w/o the hash.

        do {
            Array* array1 = Cell_Array(val1); // val1 and val2 swapped 2nd pass!

            // Check what is in series1 but not in series2
            //
            if (flags & SOP_FLAG_CHECK)
                hser = Hash_Block(val2, skip, cased);

            // Iterate over first series
            //
            i = VAL_INDEX(val1);
            for (; i < ARR_LEN(array1); i += skip) {
                Cell* item = Array_At(array1, i);
                if (flags & SOP_FLAG_CHECK) {
                    h = Find_Key_Hashed(
                        Cell_Array(val2),
                        hser,
                        item,
                        VAL_SPECIFIER(val1),
                        skip,
                        cased,
                        1
                    );
                    h = (h >= 0);
                    if (flags & SOP_FLAG_INVERT) h = !h;
                }
                if (h) {
                    Find_Key_Hashed(
                        ARR(buffer),
                        hret,
                        item,
                        VAL_SPECIFIER(val1),
                        skip,
                        cased,
                        2
                    );
                }
            }

            if (i != ARR_LEN(array1)) {
                //
                // In the current philosophy, the semantics of what to do
                // with things like `intersect/skip [1 2 3] [7] 2` is too
                // shaky to deal with, so an error is reported if it does
                // not work out evenly to the skip size.
                //
                fail (Error_Block_Skip_Wrong_Raw());
            }

            if (flags & SOP_FLAG_CHECK)
                Free_Unmanaged_Series(hser);

            if (not first_pass)
                break;
            first_pass = false;

            // Iterate over second series?
            //
            if ((i = ((flags & SOP_FLAG_BOTH) != 0))) {
                const Value* temp = val1;
                val1 = val2;
                val2 = temp;
            }
        } while (i);

        if (hret)
            Free_Unmanaged_Series(hret);

        // The buffer may have been allocated too large, so copy it at the
        // used capacity size
        //
        out_ser = SER(Copy_Array_Shallow(ARR(buffer), SPECIFIED));
        Free_Unmanaged_Array(ARR(buffer));
    }
    else if (ANY_STRING(val1)) {
        DECLARE_MOLD (mo);

        // ask mo->series to have at least `i` capacity beyond mo->start
        //
        SET_MOLD_FLAG(mo, MOLD_FLAG_RESERVE);
        mo->reserve = i;
        Push_Mold(mo);

        do {
            REBSER *ser = VAL_SERIES(val1); // val1 and val2 swapped 2nd pass!
            REBUNI uc;

            // Iterate over first series
            //
            i = VAL_INDEX(val1);
            for (; i < SER_LEN(ser); i += skip) {
                uc = GET_ANY_CHAR(ser, i);
                if (flags & SOP_FLAG_CHECK) {
                    h = (NOT_FOUND != Find_Str_Char(
                        uc,
                        VAL_SERIES(val2),
                        0,
                        VAL_INDEX(val2),
                        VAL_LEN_HEAD(val2),
                        skip,
                        cased ? AM_FIND_CASE : 0
                    ));

                    if (flags & SOP_FLAG_INVERT) h = !h;
                }

                if (!h) continue;

                if (
                    NOT_FOUND == Find_Str_Char(
                        uc, // c2 (the character to find)
                        mo->series, // ser
                        mo->start, // head
                        mo->start, // index
                        SER_LEN(mo->series), // tail
                        skip, // skip
                        cased ? AM_FIND_CASE : 0 // flags
                    )
                ){
                    DECLARE_VALUE (temp);
                    Init_Any_Series_At(temp, REB_TEXT, ser, i);
                    Append_Utf8_String(mo->series, temp, skip);
                }
            }

            if (not first_pass)
                break;
            first_pass = false;

            // Iterate over second series?
            //
            if ((i = ((flags & SOP_FLAG_BOTH) != 0))) {
                const Value* temp = val1;
                val1 = val2;
                val2 = temp;
            }
        } while (i);

        out_ser = Pop_Molded_String(mo);
    }
    else {
        assert(IS_BINARY(val1) and IS_BINARY(val2));

        DECLARE_MOLD (mo);

        // All binaries use "case-sensitive" comparison (e.g. each byte
        // is treated distinctly)
        //
        cased = true;

        // ask mo->series to have at least `i` capacity beyond mo->start
        //
        SET_MOLD_FLAG(mo, MOLD_FLAG_RESERVE);
        mo->reserve = i;
        Push_Mold(mo);

        do {
            REBSER *ser = VAL_SERIES(val1); // val1 and val2 swapped 2nd pass!
            REBUNI uc;

            // Iterate over first series
            //
            i = VAL_INDEX(val1);
            for (; i < SER_LEN(ser); i += skip) {
                uc = GET_ANY_CHAR(ser, i);
                if (flags & SOP_FLAG_CHECK) {
                    h = (NOT_FOUND != Find_Str_Char(
                        uc,
                        VAL_SERIES(val2),
                        0,
                        VAL_INDEX(val2),
                        VAL_LEN_HEAD(val2),
                        skip,
                        cased ? AM_FIND_CASE : 0
                    ));

                    if (flags & SOP_FLAG_INVERT) h = !h;
                }

                if (!h) continue;

                if (
                    NOT_FOUND == Find_Str_Char(
                        uc, // c2 (the character to find)
                        mo->series, // ser
                        mo->start, // head
                        mo->start, // index
                        SER_LEN(mo->series), // tail
                        skip, // skip
                        cased ? AM_FIND_CASE : 0 // flags
                    )
                ){
                    // This would append non-valid UTF-8 to the mold buffer.
                    // There should probably be a byte buffer.
                    //
                    fail ("Binary set operations temporarily unsupported.");

                    // Append_String(mo->series, ser, i, skip);
                }
            }

            if (not first_pass)
                break;
            first_pass = false;

            // Iterate over second series?
            //
            if ((i = ((flags & SOP_FLAG_BOTH) != 0))) {
                const Value* temp = val1;
                val1 = val2;
                val2 = temp;
            }
        } while (i);

        out_ser = Pop_Molded_Binary(mo);
    }

    return out_ser;
}


//
//  exclude: native [
//
//  {Returns the first data set less the second data set.}
//
//      series [any-array! any-string! binary! bitset! typeset!]
//          "original data"
//      exclusions [any-array! any-string! binary! bitset! typeset!]
//          "data to exclude from series"
//      /case
//          "Uses case-sensitive comparison"
//      /skip
//          "Treat the series as records of fixed size"
//      size [integer!]
//  ]
//
DECLARE_NATIVE(exclude)
{
    INCLUDE_PARAMS_OF_EXCLUDE;

    Value* val1 = ARG(series);
    Value* val2 = ARG(exclusions);

    if (IS_BITSET(val1) || IS_BITSET(val2)) {
        if (VAL_TYPE(val1) != VAL_TYPE(val2))
            fail (Error_Unexpected_Type(VAL_TYPE(val1), VAL_TYPE(val2)));

        DECLARE_VALUE (verb); // initial code did something weird w/this
        Init_Word(verb, Canon(SYM_EXCLUDE));
        return Init_Bitset(OUT, Xandor_Binary(verb, val1, val2));
    }

    if (IS_TYPESET(val1) || IS_TYPESET(val2)) {
        if (VAL_TYPE(val1) != VAL_TYPE(val2))
            fail (Error_Unexpected_Type(VAL_TYPE(val1), VAL_TYPE(val2)));

        Copy_Cell(OUT, val1);
        VAL_TYPESET_BITS(OUT) &= ~VAL_TYPESET_BITS(val2);
        return OUT;
    }

    return Init_Any_Series(
        OUT,
        VAL_TYPE(val1),
        Make_Set_Operation_Series(
            val1,
            val2,
            SOP_FLAG_CHECK | SOP_FLAG_INVERT,
            REF(case),
            REF(skip) ? Int32s(ARG(size), 1) : 1
        )
    );
}


//
//  unique: native [
//
//  "Returns the data set with duplicates removed."
//
//      series [any-array! any-string! binary! bitset! typeset!]
//      /case
//          "Use case-sensitive comparison (except bitsets)"
//      /skip
//          "Treat the series as records of fixed size"
//      size [integer!]
//  ]
//
DECLARE_NATIVE(unique)
{
    INCLUDE_PARAMS_OF_UNIQUE;

    Value* val = ARG(series);

    if (IS_BITSET(val) or IS_TYPESET(val))
        return val; // bitsets & typesets already unique (by definition)

    return Init_Any_Series(
        OUT,
        VAL_TYPE(val),
        Make_Set_Operation_Series(
            val,
            nullptr,
            SOP_NONE,
            REF(case),
            REF(skip) ? Int32s(ARG(size), 1) : 1
        )
    );
}
