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
//  Make_Set_Operation_Flex: C
//
// Do set operations on a series.  Case-sensitive if `cased` is TRUE.
// `skip` is the record size.
//
Flex* Make_Set_Operation_Flex(
    const Value* val1,
    const Value* val2,
    REBFLGS flags,
    bool cased,
    REBLEN skip
){
    assert(Any_Series(val1));

    if (val2) {
        assert(Any_Series(val2));

        if (Any_List(val1)) {
            if (!Any_List(val2))
                fail (Error_Unexpected_Type(VAL_TYPE(val1), VAL_TYPE(val2)));

            // As long as they're both arrays, we're willing to do:
            //
            //     >> union the (a b c) 'b/d/e
            //     (a b c d e)
            //
            // The type of the result will match the first value.
        }
        else if (Any_String(val1)) {

            // We will similarly do any two ANY-STRING! types:
            //
            //      >> union <abc> "bde"
            //      <abcde>

            if (not Any_String((val2)))
                fail (Error_Unexpected_Type(VAL_TYPE(val1), VAL_TYPE(val2)));
        }
        else {
            // Binaries only operate with other binaries
            assert(Is_Binary(val1));
            if (not Is_Binary(val2))
                fail (Error_Unexpected_Type(VAL_TYPE(val1), VAL_TYPE(val2)));
        }
    }

    // Calculate `i` as maximum length of result block.  The temporary buffer
    // will be allocated at this size, but copied out at the exact size of
    // the actual result.
    //
    REBLEN i = Cell_Series_Len_At(val1);
    if (flags & SOP_FLAG_BOTH)
        i += Cell_Series_Len_At(val2);

    REBINT h = 1; // used for both logic true/false and hash check
    bool first_pass = true; // are we in the first pass over the series?
    Flex* out_ser;

    if (Any_List(val1)) {
        Flex* hser = 0;   // hash table for series
        Flex* hret;       // hash table for return series

        // The buffer used for building the return series.  This creates
        // a new buffer every time, but reusing one might be slightly more
        // efficient.
        //
        Array* buffer = Make_Array(i);
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
            for (; i < Array_Len(array1); i += skip) {
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
                        cast_Array(buffer),
                        hret,
                        item,
                        VAL_SPECIFIER(val1),
                        skip,
                        cased,
                        2
                    );
                }
            }

            if (i != Array_Len(array1)) {
                //
                // In the current philosophy, the semantics of what to do
                // with things like `intersect/skip [1 2 3] [7] 2` is too
                // shaky to deal with, so an error is reported if it does
                // not work out evenly to the skip size.
                //
                fail (Error_Block_Skip_Wrong_Raw());
            }

            if (flags & SOP_FLAG_CHECK)
                Free_Unmanaged_Flex(hser);

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
            Free_Unmanaged_Flex(hret);

        // The buffer may have been allocated too large, so copy it at the
        // used capacity size
        //
        out_ser = Copy_Array_Shallow(cast_Array(buffer), SPECIFIED);
        Free_Unmanaged_Flex(cast_Array(buffer));
    }
    else if (Any_String(val1)) {
        DECLARE_MOLD (mo);

        // ask mo->series to have at least `i` capacity beyond mo->start
        //
        SET_MOLD_FLAG(mo, MOLD_FLAG_RESERVE);
        mo->reserve = i;
        Push_Mold(mo);

        do {
            Flex* flex = Cell_Flex(val1); // val1 and val2 swapped 2nd pass!
            REBUNI uc;

            // Iterate over first series
            //
            i = VAL_INDEX(val1);
            for (; i < Flex_Len(flex); i += skip) {
                uc = GET_ANY_CHAR(flex, i);
                if (flags & SOP_FLAG_CHECK) {
                    h = (NOT_FOUND != Find_Str_Char(
                        uc,
                        Cell_Flex(val2),
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
                        mo->series, // flex
                        mo->start, // head
                        mo->start, // index
                        Flex_Len(mo->series), // tail
                        skip, // skip
                        cased ? AM_FIND_CASE : 0 // flags
                    )
                ){
                    DECLARE_VALUE (temp);
                    Init_Any_Series_At(temp, REB_TEXT, flex, i);
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
        assert(Is_Binary(val1) and Is_Binary(val2));

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
            Flex* flex = Cell_Flex(val1); // val1 and val2 swapped 2nd pass!
            REBUNI uc;

            // Iterate over first series
            //
            i = VAL_INDEX(val1);
            for (; i < Flex_Len(flex); i += skip) {
                uc = GET_ANY_CHAR(flex, i);
                if (flags & SOP_FLAG_CHECK) {
                    h = (NOT_FOUND != Find_Str_Char(
                        uc,
                        Cell_Flex(val2),
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
                        mo->series, // flex
                        mo->start, // head
                        mo->start, // index
                        Flex_Len(mo->series), // tail
                        skip, // skip
                        cased ? AM_FIND_CASE : 0 // flags
                    )
                ){
                    // This would append non-valid UTF-8 to the mold buffer.
                    // There should probably be a byte buffer.
                    //
                    fail ("Binary set operations temporarily unsupported.");

                    // Append_String(mo->series, flex, i, skip);
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
//      series [any-list! any-string! binary! bitset! typeset!]
//          "original data"
//      exclusions [any-list! any-string! binary! bitset! typeset!]
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

    if (Is_Bitset(val1) || Is_Bitset(val2)) {
        if (VAL_TYPE(val1) != VAL_TYPE(val2))
            fail (Error_Unexpected_Type(VAL_TYPE(val1), VAL_TYPE(val2)));

        DECLARE_VALUE (verb); // initial code did something weird w/this
        Init_Word(verb, Canon(SYM_EXCLUDE));
        return Init_Bitset(OUT, Xandor_Binary(verb, val1, val2));
    }

    if (Is_Typeset(val1) || Is_Typeset(val2)) {
        if (VAL_TYPE(val1) != VAL_TYPE(val2))
            fail (Error_Unexpected_Type(VAL_TYPE(val1), VAL_TYPE(val2)));

        Copy_Cell(OUT, val1);
        VAL_TYPESET_BITS(OUT) &= ~VAL_TYPESET_BITS(val2);
        return OUT;
    }

    return Init_Any_Series(
        OUT,
        VAL_TYPE(val1),
        Make_Set_Operation_Flex(
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
//      series [any-list! any-string! binary! bitset! typeset!]
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

    if (Is_Bitset(val) or Is_Typeset(val))
        return val; // bitsets & typesets already unique (by definition)

    return Init_Any_Series(
        OUT,
        VAL_TYPE(val),
        Make_Set_Operation_Flex(
            val,
            nullptr,
            SOP_NONE,
            REF(case),
            REF(skip) ? Int32s(ARG(size), 1) : 1
        )
    );
}
