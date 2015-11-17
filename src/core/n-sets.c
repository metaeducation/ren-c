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
**  Module:  n-sets.c
**  Summary: native functions for data sets
**  Section: natives
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

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
static REBSER *Make_Set_Operation_Series(const REBVAL *val1, const REBVAL *val2, REBCNT flags, REBCNT cased, REBCNT skip)
{
    REBSER *buffer;     // buffer for building the return series
    REBCNT i;
    REBINT h = TRUE;
    REBFLG first_pass = TRUE; // are we in the first pass over the series?
    REBSER *out_ser;

    // This routine should only be called with SERIES! values
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
        else if (!IS_BINARY(val1)) {

            // We will similarly do any two ANY-STRING! types:
            //
            //      >> union <abc> "bde"
            //      <abcde>

            if (IS_BINARY(val2))
                fail (Error_Unexpected_Type(VAL_TYPE(val1), VAL_TYPE(val2)));
        }
        else {
            // Binaries only operate with other binaries

            if (!IS_BINARY(val2))
                fail (Error_Unexpected_Type(VAL_TYPE(val1), VAL_TYPE(val2)));
        }
    }

    // Calculate i as length of result block.
    i = VAL_LEN(val1);
    if (flags & SOP_FLAG_BOTH) i += VAL_LEN(val2);

    if (ANY_ARRAY(val1)) {
        REBSER *hser = 0;   // hash table for series
        REBSER *hret;       // hash table for return series

        buffer = BUF_EMIT;          // use preallocated shared block
        Resize_Series(buffer, i);
        hret = Make_Hash_Sequence(i);   // allocated

        // Optimization note: !!
        // This code could be optimized for small blocks by not hashing them
        // and extending Find_Key to do a FIND on the value itself w/o the hash.

        do {
            REBSER *ser = VAL_SERIES(val1); // val1 and val2 swapped 2nd pass!

            // Check what is in series1 but not in series2:
            if (flags & SOP_FLAG_CHECK)
                hser = Hash_Block(val2, cased);

            // Iterate over first series:
            i = VAL_INDEX(val1);
            for (; i < SERIES_TAIL(ser); i += skip) {
                REBVAL *item = BLK_SKIP(ser, i);
                if (flags & SOP_FLAG_CHECK) {
                    h = Find_Key(VAL_SERIES(val2), hser, item, skip, cased, 1);
                    h = (h >= 0);
                    if (flags & SOP_FLAG_INVERT) h = !h;
                }
                if (h) Find_Key(buffer, hret, item, skip, cased, 2);
            }

            if (flags & SOP_FLAG_CHECK)
                Free_Series(hser);

            if (!first_pass) break;
            first_pass = FALSE;

            // Iterate over second series?
            if ((i = ((flags & SOP_FLAG_BOTH) != 0))) {
                const REBVAL *temp = val1;
                val1 = val2;
                val2 = temp;
            }
        } while (i);

        if (hret)
            Free_Series(hret);

        out_ser = Copy_Array_Shallow(buffer);
        RESET_TAIL(buffer); // required - allow reuse
    }
    else {
        if (IS_BINARY(val1)) {
            // All binaries use "case-sensitive" comparison (e.g. each byte
            // is treated distinctly)
            cased = TRUE;
        }

        buffer = BUF_MOLD;
        Reset_Buffer(buffer, i);
        RESET_TAIL(buffer);

        do {
            REBSER *ser = VAL_SERIES(val1); // val1 and val2 swapped 2nd pass!
            REBUNI uc;

            // Iterate over first series:
            i = VAL_INDEX(val1);
            for (; i < SERIES_TAIL(ser); i += skip) {
                uc = GET_ANY_CHAR(ser, i);
                if (flags & SOP_FLAG_CHECK) {
                    h = (NOT_FOUND != Find_Str_Char(
                        VAL_SERIES(val2),
                        0,
                        VAL_INDEX(val2),
                        VAL_TAIL(val2),
                        skip,
                        uc,
                        cased ? AM_FIND_CASE : 0
                    ));

                    if (flags & SOP_FLAG_INVERT) h = !h;
                }

                if (!h) continue;

                if (
                    NOT_FOUND == Find_Str_Char(
                        buffer,
                        0,
                        0,
                        SERIES_TAIL(buffer),
                        skip,
                        uc,
                        cased ? AM_FIND_CASE : 0
                    )
                ) {
                    Append_String(buffer, ser, i, skip);
                }
            }

            if (!first_pass) break;
            first_pass = FALSE;

            // Iterate over second series?
            if ((i = ((flags & SOP_FLAG_BOTH) != 0))) {
                const REBVAL *temp = val1;
                val1 = val2;
                val2 = temp;
            }
        } while (i);

        out_ser = Copy_String(buffer, 0, -1);
    }

    return out_ser;
}


//
//  difference: native [
//  
//  "Returns the special difference of two values."
//  
//      set1 [any-array! any-string! binary! bitset! date! typeset!] 
//      "First data set"
//      set2 [any-array! any-string! binary! bitset! date! typeset!] 
//      "Second data set"
//      /case "Uses case-sensitive comparison"
//      /skip "Treat the series as records of fixed size"
//      size [integer!]
//  ]
//
REBNATIVE(difference)
{
    REBVAL *val1 = D_ARG(1);
    REBVAL *val2 = D_ARG(2);

    const REBOOL cased = D_REF(3);
    const REBOOL skip = D_REF(4) ? Int32s(D_ARG(5), 1) : 1;

    // Plain SUBTRACT on dates has historically given a count of days.
    // DIFFERENCE has been the way to get the time difference.
    // !!! Is this sensible?
    if (IS_DATE(val1) || IS_DATE(val2)) {
        if (VAL_TYPE(val1) != VAL_TYPE(val2))
            fail (Error_Unexpected_Type(VAL_TYPE(val1), VAL_TYPE(val2)));

        Subtract_Date(val1, val2, D_OUT);
        return R_OUT;
    }

    if (IS_BITSET(val1) || IS_BITSET(val2)) {
        if (VAL_TYPE(val1) != VAL_TYPE(val2))
            fail (Error_Unexpected_Type(VAL_TYPE(val1), VAL_TYPE(val2)));

        Val_Init_Bitset(D_OUT, Xandor_Binary(A_XOR, val1, val2));
        return R_OUT;
    }

    if (IS_TYPESET(val1) || IS_TYPESET(val2)) {
        if (VAL_TYPE(val1) != VAL_TYPE(val2))
            fail (Error_Unexpected_Type(VAL_TYPE(val1), VAL_TYPE(val2)));

        *D_OUT = *val1;
        VAL_TYPESET_BITS(D_OUT) ^= VAL_TYPESET_BITS(val2);
        return R_OUT;
    }

    Val_Init_Series(
        D_OUT,
        VAL_TYPE(val1),
        Make_Set_Operation_Series(
            val1,
            val2,
            SOP_FLAG_BOTH | SOP_FLAG_CHECK | SOP_FLAG_INVERT,
            cased,
            skip
        )
    );
    return R_OUT;
}


//
//  exclude: native [
//  
//  {Returns the first data set less the second data set.}
//  
//      set1 [any-array! any-string! binary! bitset! typeset!] "First data set"
//      
//      set2 [any-array! any-string! binary! bitset! typeset!] 
//      "Second data set"
//      /case "Uses case-sensitive comparison"
//      /skip "Treat the series as records of fixed size"
//      size [integer!]
//  ]
//
REBNATIVE(exclude)
{
    REBVAL *val1 = D_ARG(1);
    REBVAL *val2 = D_ARG(2);

    const REBOOL cased = D_REF(3);
    const REBOOL skip = D_REF(4) ? Int32s(D_ARG(5), 1) : 1;

    if (IS_BITSET(val1) || IS_BITSET(val2)) {
        if (VAL_TYPE(val1) != VAL_TYPE(val2))
            fail (Error_Unexpected_Type(VAL_TYPE(val1), VAL_TYPE(val2)));

        // !!! 0 was said to be a "special case" in original code
        Val_Init_Bitset(D_OUT, Xandor_Binary(0, val1, val2));
        return R_OUT;
    }

    if (IS_TYPESET(val1) || IS_TYPESET(val2)) {
        if (VAL_TYPE(val1) != VAL_TYPE(val2))
            fail (Error_Unexpected_Type(VAL_TYPE(val1), VAL_TYPE(val2)));

        *D_OUT = *val1;
        VAL_TYPESET_BITS(D_OUT) &= ~VAL_TYPESET_BITS(val2);
        return R_OUT;
    }

    Val_Init_Series(
        D_OUT,
        VAL_TYPE(val1),
        Make_Set_Operation_Series(
            val1, val2, SOP_FLAG_CHECK | SOP_FLAG_INVERT, cased, skip
        )
    );
    return R_OUT;
}


//
//  intersect: native [
//  
//  "Returns the intersection of two data sets."
//  
//      set1 [any-array! any-string! binary! bitset! typeset!] "first set"
//      set2 [any-array! any-string! binary! bitset! typeset!] "second set"
//      /case "Uses case-sensitive comparison"
//      /skip "Treat the series as records of fixed size"
//      size [integer!]
//  ]
//
REBNATIVE(intersect)
{
    REBVAL *val1 = D_ARG(1);
    REBVAL *val2 = D_ARG(2);

    const REBOOL cased = D_REF(3);
    const REBOOL skip = D_REF(4) ? Int32s(D_ARG(5), 1) : 1;

    if (IS_BITSET(val1) || IS_BITSET(val2)) {
        if (VAL_TYPE(val1) != VAL_TYPE(val2))
            fail (Error_Unexpected_Type(VAL_TYPE(val1), VAL_TYPE(val2)));

        Val_Init_Bitset(D_OUT, Xandor_Binary(A_AND, val1, val2));
        return R_OUT;
    }

    if (IS_TYPESET(val1) || IS_TYPESET(val2)) {
        if (VAL_TYPE(val1) != VAL_TYPE(val2))
            fail (Error_Unexpected_Type(VAL_TYPE(val1), VAL_TYPE(val2)));

        *D_OUT = *val1;
        VAL_TYPESET_BITS(D_OUT) &= VAL_TYPESET_BITS(val2);
        return R_OUT;
    }

    Val_Init_Series(
        D_OUT,
        VAL_TYPE(val1),
        Make_Set_Operation_Series(
            val1, val2, SOP_FLAG_CHECK, cased, skip
        )
    );
    return R_OUT;
}


//
//  union: native [
//  
//  "Returns the union of two data sets."
//  
//      set1 [any-array! any-string! binary! bitset! typeset!] "first set"
//      set2 [any-array! any-string! binary! bitset! typeset!] "second set"
//      /case "Use case-sensitive comparison"
//      /skip "Treat the series as records of fixed size"
//      size [integer!]
//  ]
//
REBNATIVE(union)
{
    REBVAL *val1 = D_ARG(1);
    REBVAL *val2 = D_ARG(2);

    const REBOOL cased = D_REF(3);
    const REBOOL skip = D_REF(4) ? Int32s(D_ARG(5), 1) : 1;

    if (IS_BITSET(val1) || IS_BITSET(val2)) {
        if (VAL_TYPE(val1) != VAL_TYPE(val2))
            fail (Error_Unexpected_Type(VAL_TYPE(val1), VAL_TYPE(val2)));

        Val_Init_Bitset(D_OUT, Xandor_Binary(A_OR, val1, val2));
        return R_OUT;
    }

    if (IS_TYPESET(val1) || IS_TYPESET(val2)) {
        if (VAL_TYPE(val1) != VAL_TYPE(val2))
            fail (Error_Unexpected_Type(VAL_TYPE(val1), VAL_TYPE(val2)));

        *D_OUT = *val1;
        VAL_TYPESET_BITS(D_OUT) |= VAL_TYPESET_BITS(val2);
        return R_OUT;
    }

    Val_Init_Series(
        D_OUT,
        VAL_TYPE(val1),
        Make_Set_Operation_Series(
            val1, val2, SOP_FLAG_BOTH, cased, skip
        )
    );
    return R_OUT;
}


//
//  unique: native [
//  
//  "Returns the data set with duplicates removed."
//  
//      set1 [any-array! any-string! binary! bitset! typeset!]
//      /case "Use case-sensitive comparison (except bitsets)"
//      /skip "Treat the series as records of fixed size"
//      size [integer!]
//  ]
//
REBNATIVE(unique)
{
    REBVAL *val1 = D_ARG(1);

    const REBOOL cased = D_REF(2);
    const REBOOL skip = D_REF(3) ? Int32s(D_ARG(4), 1) : 1;

    if (IS_BITSET(val1) || IS_TYPESET(val1)) {
        // Bitsets and typesets already unique (by definition)
        return R_ARG1;
    }

    Val_Init_Series(
        D_OUT,
        VAL_TYPE(val1),
        Make_Set_Operation_Series(val1, NULL, SOP_NONE, cased, skip)
    );
    return R_OUT;
}
