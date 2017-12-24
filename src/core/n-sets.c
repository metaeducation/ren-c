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

enum {
    SOP_NONE = 0, // used by UNIQUE (other flags do not apply)
    SOP_FLAG_BOTH = 1 << 0, // combine and interate over both series
    SOP_FLAG_CHECK = 1 << 1, // check other series for value existence
    SOP_FLAG_INVERT = 1 << 2 // invert the result of the search
};


//
//  Make_Set_Operation_Series: C
//
// Do set operations on a series.  Case-sensitive if `cased` is TRUE.
// `skip` is the record size.
//
static REBSER *Make_Set_Operation_Series(
    const REBVAL *val1,
    const REBVAL *val2,
    REBFLGS flags,
    REBOOL cased,
    REBCNT skip
) {
    REBCNT i;
    REBINT h = 1; // used for both logic true/false and hash check
    REBOOL first_pass = TRUE; // are we in the first pass over the series?
    REBSER *out_ser;

    assert(ANY_SERIES(val1));

    if (val2) {
        assert(ANY_SERIES(val2));

        if (ANY_ARRAY(val1)) {
            if (!ANY_ARRAY(val2))
                fail (Error_Unexpected_Type(VAL_TYPE(val1), VAL_TYPE(val2)));

            // As long as they're both arrays, we're willing to do:
            //
            //     >> union quote (a b c) 'b/d/e
            //     (a b c d e)
            //
            // The type of the result will match the first value.
        }
        else if (ANY_STRING(val1)) {

            // We will similarly do any two ANY-STRING! types:
            //
            //      >> union <abc> "bde"
            //      <abcde>

            if (NOT(ANY_STRING((val2))))
                fail (Error_Unexpected_Type(VAL_TYPE(val1), VAL_TYPE(val2)));
        }
        else {
            // Binaries only operate with other binaries
            assert(IS_BINARY(val1));
            if (!IS_BINARY(val2))
                fail (Error_Unexpected_Type(VAL_TYPE(val1), VAL_TYPE(val2)));
        }
    }

    // Calculate `i` as maximum length of result block.  The temporary buffer
    // will be allocated at this size, but copied out at the exact size of
    // the actual result.
    //
    i = VAL_LEN_AT(val1);
    if (flags & SOP_FLAG_BOTH) i += VAL_LEN_AT(val2);

    if (ANY_ARRAY(val1)) {
        REBSER *hser = 0;   // hash table for series
        REBSER *hret;       // hash table for return series

        // The buffer used for building the return series.  This creates
        // a new buffer every time, but reusing one might be slightly more
        // efficient.
        //
        REBSER *buffer = SER(Make_Array(i));
        hret = Make_Hash_Sequence(i);   // allocated

        // Optimization note: !!
        // This code could be optimized for small blocks by not hashing them
        // and extending Find_Key to FIND on the value itself w/o the hash.

        do {
            REBARR *array1 = VAL_ARRAY(val1); // val1 and val2 swapped 2nd pass!

            // Check what is in series1 but not in series2
            //
            if (flags & SOP_FLAG_CHECK)
                hser = Hash_Block(val2, skip, cased);

            // Iterate over first series
            //
            i = VAL_INDEX(val1);
            for (; i < ARR_LEN(array1); i += skip) {
                RELVAL *item = ARR_AT(array1, i);
                if (flags & SOP_FLAG_CHECK) {
                    h = Find_Key_Hashed(
                        VAL_ARRAY(val2),
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
                Free_Series(hser);

            if (!first_pass) break;
            first_pass = FALSE;

            // Iterate over second series?
            //
            if ((i = ((flags & SOP_FLAG_BOTH) != 0))) {
                const REBVAL *temp = val1;
                val1 = val2;
                val2 = temp;
            }
        } while (i);

        if (hret)
            Free_Series(hret);

        // The buffer may have been allocated too large, so copy it at the
        // used capacity size
        //
        out_ser = SER(Copy_Array_Shallow(ARR(buffer), SPECIFIED));
        Free_Array(ARR(buffer));
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
                    DECLARE_LOCAL (temp);
                    Init_Any_Series_At(temp, REB_STRING, ser, i);
                    Append_Utf8_String(mo->series, temp, skip);
                }
            }

            if (!first_pass) break;
            first_pass = FALSE;

            // Iterate over second series?
            //
            if ((i = ((flags & SOP_FLAG_BOTH) != 0))) {
                const REBVAL *temp = val1;
                val1 = val2;
                val2 = temp;
            }
        } while (i);

        out_ser = Pop_Molded_String(mo);
    }
    else {
        assert(IS_BINARY(val1) && IS_BINARY(val2));

        DECLARE_MOLD (mo);

        // All binaries use "case-sensitive" comparison (e.g. each byte
        // is treated distinctly)
        //
        cased = TRUE;

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

            if (!first_pass) break;
            first_pass = FALSE;

            // Iterate over second series?
            //
            if ((i = ((flags & SOP_FLAG_BOTH) != 0))) {
                const REBVAL *temp = val1;
                val1 = val2;
                val2 = temp;
            }
        } while (i);

        out_ser = Pop_Molded_Binary(mo);
    }

    return out_ser;
}


//
//  difference: native [
//
//  "Returns the special difference of two values."
//
//      series1 [any-array! any-string! binary! bitset! date! typeset!]
//      series2 [any-array! any-string! binary! bitset! date! typeset!]
//      /case
//          "Uses case-sensitive comparison"
//      /skip
//          "Treat the series as records of fixed size"
//      size [integer!]
//  ]
//
REBNATIVE(difference)
{
    INCLUDE_PARAMS_OF_DIFFERENCE;

    REBVAL *val1 = ARG(series1);
    REBVAL *val2 = ARG(series2);

    // Plain SUBTRACT on dates has historically given a count of days.
    // DIFFERENCE has been the way to get the time difference.
    // !!! Is this sensible?
    //
    if (IS_DATE(val1) || IS_DATE(val2)) {
        if (VAL_TYPE(val1) != VAL_TYPE(val2))
            fail (Error_Unexpected_Type(VAL_TYPE(val1), VAL_TYPE(val2)));

        Subtract_Date(val1, val2, D_OUT);
        return R_OUT;
    }

    if (IS_BITSET(val1) || IS_BITSET(val2)) {
        if (VAL_TYPE(val1) != VAL_TYPE(val2))
            fail (Error_Unexpected_Type(VAL_TYPE(val1), VAL_TYPE(val2)));

        Init_Bitset(D_OUT, Xandor_Binary(SYM_XOR_T, val1, val2));
        return R_OUT;
    }

    if (IS_TYPESET(val1) || IS_TYPESET(val2)) {
        if (VAL_TYPE(val1) != VAL_TYPE(val2))
            fail (Error_Unexpected_Type(VAL_TYPE(val1), VAL_TYPE(val2)));

        Move_Value(D_OUT, val1);
        VAL_TYPESET_BITS(D_OUT) ^= VAL_TYPESET_BITS(val2);
        return R_OUT;
    }

    Init_Any_Series(
        D_OUT,
        VAL_TYPE(val1),
        Make_Set_Operation_Series(
            val1,
            val2,
            SOP_FLAG_BOTH | SOP_FLAG_CHECK | SOP_FLAG_INVERT,
            REF(case),
            REF(skip) ? Int32s(ARG(size), 1) : 1
        )
    );
    return R_OUT;
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
REBNATIVE(exclude)
{
    INCLUDE_PARAMS_OF_EXCLUDE;

    REBVAL *val1 = ARG(series);
    REBVAL *val2 = ARG(exclusions);

    if (IS_BITSET(val1) || IS_BITSET(val2)) {
        if (VAL_TYPE(val1) != VAL_TYPE(val2))
            fail (Error_Unexpected_Type(VAL_TYPE(val1), VAL_TYPE(val2)));

        // !!! 0 was said to be a "special case" in original code
        //
        Init_Bitset(D_OUT, Xandor_Binary(0, val1, val2));
        return R_OUT;
    }

    if (IS_TYPESET(val1) || IS_TYPESET(val2)) {
        if (VAL_TYPE(val1) != VAL_TYPE(val2))
            fail (Error_Unexpected_Type(VAL_TYPE(val1), VAL_TYPE(val2)));

        Move_Value(D_OUT, val1);
        VAL_TYPESET_BITS(D_OUT) &= ~VAL_TYPESET_BITS(val2);
        return R_OUT;
    }

    Init_Any_Series(
        D_OUT,
        VAL_TYPE(val1),
        Make_Set_Operation_Series(
            val1,
            val2,
            SOP_FLAG_CHECK | SOP_FLAG_INVERT,
            REF(case),
            REF(skip) ? Int32s(ARG(size), 1) : 1
        )
    );
    return R_OUT;
}


//
//  intersect: native [
//
//  "Returns the intersection of two data series."
//
//      series1 [any-array! any-string! binary! bitset! typeset!]
//      series2 [any-array! any-string! binary! bitset! typeset!]
//      /case
//          "Uses case-sensitive comparison"
//      /skip
//          "Treat the series as records of fixed size"
//      size [integer!]
//  ]
//
REBNATIVE(intersect)
{
    INCLUDE_PARAMS_OF_INTERSECT;

    REBVAL *val1 = ARG(series1);
    REBVAL *val2 = ARG(series2);

    if (IS_BITSET(val1) || IS_BITSET(val2)) {
        if (VAL_TYPE(val1) != VAL_TYPE(val2))
            fail (Error_Unexpected_Type(VAL_TYPE(val1), VAL_TYPE(val2)));

        Init_Bitset(D_OUT, Xandor_Binary(SYM_AND_T, val1, val2));
        return R_OUT;
    }

    if (IS_TYPESET(val1) || IS_TYPESET(val2)) {
        if (VAL_TYPE(val1) != VAL_TYPE(val2))
            fail (Error_Unexpected_Type(VAL_TYPE(val1), VAL_TYPE(val2)));

        Move_Value(D_OUT, val1);
        VAL_TYPESET_BITS(D_OUT) &= VAL_TYPESET_BITS(val2);
        return R_OUT;
    }

    Init_Any_Series(
        D_OUT,
        VAL_TYPE(val1),
        Make_Set_Operation_Series(
            val1,
            val2,
            SOP_FLAG_CHECK,
            REF(case),
            REF(skip) ? Int32s(ARG(size), 1) : 1
        )
    );

    return R_OUT;
}


//
//  union: native [
//
//  "Returns the union of two data series."
//
//      series1 [any-array! any-string! binary! bitset! typeset!]
//      series2 [any-array! any-string! binary! bitset! typeset!]
//      /case
//          "Use case-sensitive comparison"
//      /skip
//          "Treat the series as records of fixed size"
//      size [integer!]
//  ]
//
REBNATIVE(union)
{
    INCLUDE_PARAMS_OF_UNION;

    REBVAL *val1 = ARG(series1);
    REBVAL *val2 = ARG(series2);

    if (IS_BITSET(val1) || IS_BITSET(val2)) {
        if (VAL_TYPE(val1) != VAL_TYPE(val2))
            fail (Error_Unexpected_Type(VAL_TYPE(val1), VAL_TYPE(val2)));

        Init_Bitset(D_OUT, Xandor_Binary(SYM_OR_T, val1, val2));
        return R_OUT;
    }

    if (IS_TYPESET(val1) || IS_TYPESET(val2)) {
        if (VAL_TYPE(val1) != VAL_TYPE(val2))
            fail (Error_Unexpected_Type(VAL_TYPE(val1), VAL_TYPE(val2)));

        Move_Value(D_OUT, val1);
        VAL_TYPESET_BITS(D_OUT) |= VAL_TYPESET_BITS(val2);
        return R_OUT;
    }

    Init_Any_Series(
        D_OUT,
        VAL_TYPE(val1),
        Make_Set_Operation_Series(
            val1,
            val2,
            SOP_FLAG_BOTH,
            REF(case),
            REF(skip) ? Int32s(ARG(size), 1) : 1
        )
    );

    return R_OUT;
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
REBNATIVE(unique)
{
    INCLUDE_PARAMS_OF_UNIQUE;

    REBVAL *val = ARG(series);

    if (IS_BITSET(val) || IS_TYPESET(val)) {
        //
        // Bitsets and typesets already unique (by definition)
        //
        Move_Value(D_OUT, ARG(series));
        return R_OUT;
    }

    Init_Any_Series(
        D_OUT,
        VAL_TYPE(val),
        Make_Set_Operation_Series(
            val,
            NULL,
            SOP_NONE,
            REF(case),
            REF(skip) ? Int32s(ARG(size), 1) : 1
        )
    );

    return R_OUT;
}
