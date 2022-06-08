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
// The idea of "set operations" like UNIQUE, INTERSECT, UNION, DIFFERENCE, and
// EXCLUDE were historically applicable not just to bitsets and typesets, but
// also to ANY-SERIES!.  Additionally, series were treated as *ordered*
// collections of their elements:
//
//     rebol2>> exclude "abcd" "bd"
//     == "ac"
//
//     rebol2>> exclude "dcba" "bd"
//     == "ca"
//
// Making things more complex was the introduction of a /SKIP parameter, which
// had a somewhat dubious definition of treating the series as fixed-length
// spans where the set operation was based on the first element of that span.
//
//     rebol2>> exclude/skip [a b c d] [c] 2
//     == [a b]
//
// The operations are kept here mostly in their R3-Alpha form, though they
// had to be adapted to deal with the difference between UTF-8 strings and
// binaries.
//

#include "sys-core.h"


//
//  Make_Set_Operation_Series: C
//
// Do set operations on a series.  Case-sensitive if `cased` is TRUE.
// `skip` is the record size.
//
REBSER *Make_Set_Operation_Series(
    const REBVAL *val1,
    const REBVAL *val2,
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
            //     >> union '(a b c) 'b/d/e
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
        REBSER *buffer = Make_Array(i);
        hret = Make_Hash_Series(i);   // allocated

        // Optimization note: !!
        // This code could be optimized for small blocks by not hashing them
        // and extending Find_Key to FIND on the value itself w/o the hash.

        do {
            // Note: val1 and val2 swapped 2nd pass!
            //
            const REBARR *array1 = VAL_ARRAY(val1);

            // Check what is in series1 but not in series2
            //
            if (flags & SOP_FLAG_CHECK)
                hser = Hash_Block(val2, skip, cased);

            // Iterate over first series
            //
            i = VAL_INDEX(val1);
            for (; i < ARR_LEN(array1); i += skip) {
                const Cell *item = ARR_AT(array1, i);
                if (flags & SOP_FLAG_CHECK) {
                    h = Find_Key_Hashed(
                        m_cast(REBARR*, VAL_ARRAY(val2)),  // mode 1 unchanged
                        hser,
                        item,
                        VAL_SPECIFIER(val1),
                        skip,
                        cased,
                        1  // won't modify the input array
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

            if ((flags & SOP_FLAG_BOTH) == 0)
                break;  // don't need to iterate over second series

            const REBVAL *temp = val1;
            val1 = val2;
            val2 = temp;
        } while (true);

        if (hret)
            Free_Unmanaged_Series(hret);

        // The buffer may have been allocated too large, so copy it at the
        // used capacity size
        //
        out_ser = Copy_Array_Shallow(ARR(buffer), SPECIFIED);
        Free_Unmanaged_Series(ARR(buffer));
    }
    else if (ANY_STRING(val1)) {
        DECLARE_MOLD (mo);

        // ask mo->series to have at least `i` capacity beyond mo->offset
        //
        SET_MOLD_FLAG(mo, MOLD_FLAG_RESERVE);
        mo->reserve = i;
        Push_Mold(mo);

        do {
            // Note: val1 and val2 swapped 2nd pass!
            //
            const REBSTR *str = VAL_STRING(val1);

            DECLARE_LOCAL (iter);
            Copy_Cell(iter, val1);

            // Iterate over first series
            //
            for (
                ;
                VAL_INDEX_RAW(iter) < cast(REBIDX, STR_LEN(str));
                VAL_INDEX_RAW(iter) += skip
            ){
                REBLEN len_match;

                if (flags & SOP_FLAG_CHECK) {
                    h = (NOT_FOUND != Find_Binstr_In_Binstr(
                        &len_match,
                        val2,
                        VAL_LEN_HEAD(val2),
                        iter,
                        1,  // single codepoint length
                        cased ? AM_FIND_CASE : 0,
                        skip
                    ));

                    if (flags & SOP_FLAG_INVERT) h = !h;
                }

                if (!h) continue;

                DECLARE_LOCAL (mo_value);
                Init_Cell_Header_Untracked(
                    TRACK(mo_value),
                    REB_TEXT,
                    CELL_FLAG_FIRST_IS_NODE
                );
                INIT_VAL_NODE1(mo_value, mo->series);
                VAL_INDEX_RAW(mo_value) = mo->index;

                if (
                    NOT_FOUND == Find_Binstr_In_Binstr(
                        &len_match,
                        mo_value,
                        STR_LEN(mo->series),  // tail
                        iter,
                        1,  // single codepoint length
                        cased ? AM_FIND_CASE : 0,  // flags
                        skip  // skip
                    )
                ){
                    Append_String_Limit(mo->series, iter, skip);
                }
            }

            if (not first_pass)
                break;
            first_pass = false;

            if ((flags & SOP_FLAG_BOTH) == 0)
                break;  // don't need to iterate over second series

            const REBVAL *temp = val1;
            val1 = val2;
            val2 = temp;
        } while (true);

        out_ser = Pop_Molded_String(mo);
    }
    else {
        assert(IS_BINARY(val1) and IS_BINARY(val2));

        REBBIN *buf = BYTE_BUF;
        REBLEN buf_start_len = BIN_LEN(buf);
        EXPAND_SERIES_TAIL(buf, i);  // ask for at least `i` capacity
        REBLEN buf_at = buf_start_len;

        do {
            // Note: val1 and val2 swapped 2nd pass!
            //
            const REBBIN *bin = VAL_BINARY(val1);

            // Iterate over first series
            //
            DECLARE_LOCAL (iter);
            Copy_Cell(iter, val1);

            for (
                ;
                VAL_INDEX_RAW(iter) < cast(REBIDX, BIN_LEN(bin));
                VAL_INDEX_RAW(iter) += skip
            ){
                REBLEN len_match;

                if (flags & SOP_FLAG_CHECK) {
                    h = (NOT_FOUND != Find_Binstr_In_Binstr(
                        &len_match,
                        val2,  // searched
                        VAL_LEN_HEAD(val2),  // limit (highest index)
                        iter,  // pattern
                        1,  // "part", e.g. matches only 1 byte
                        cased ? AM_FIND_CASE : 0,
                        skip
                    ));

                    if (flags & SOP_FLAG_INVERT) h = !h;
                }

                if (!h) continue;

                DECLARE_LOCAL (buf_value);
                Init_Cell_Header_Untracked(buf_value, REB_BINARY, CELL_FLAG_FIRST_IS_NODE);
                INIT_VAL_NODE1(buf_value, buf);
                VAL_INDEX_RAW(buf_value) = buf_start_len;

                if (
                    NOT_FOUND == Find_Binstr_In_Binstr(
                        &len_match,
                        buf_value,  // searched
                        VAL_LEN_HEAD(buf_value),  // limit (highest index)
                        iter,  // pattern
                        1,  // "part", e.g. matches only 1 byte
                        cased ? AM_FIND_CASE : 0,  // flags
                        skip
                    )
                ){
                    EXPAND_SERIES_TAIL(buf, skip);
                    REBSIZ size_at;
                    const REBYTE *iter_at = VAL_BINARY_SIZE_AT(&size_at, iter);
                    REBLEN min = MIN(size_at, skip);
                    memcpy(BIN_AT(buf, buf_at), iter_at, min);
                    buf_at += min;
                }
            }

            if (not first_pass)
                break;
            first_pass = false;

            if ((flags & SOP_FLAG_BOTH) == 0)
                break;  // don't need to iterate over the second series

            const REBVAL *temp = val1;
            val1 = val2;
            val2 = temp;
        } while (true);

        REBLEN out_len = buf_at - buf_start_len;
        REBBIN *out_bin = Make_Binary(out_len);
        memcpy(BIN_HEAD(out_bin), BIN_AT(buf, buf_start_len), out_len);
        TERM_BIN_LEN(out_bin, out_len);
        out_ser = out_bin;

        TERM_BIN_LEN(buf, buf_start_len);
    }

    return out_ser;
}
