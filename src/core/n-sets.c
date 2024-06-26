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
// also to ANY-SERIES?.  Additionally, series were treated as *ordered*
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
Series* Make_Set_Operation_Series(
    const Value* val1,
    const Value* val2,
    Flags flags,
    bool cased,
    REBLEN skip
){
    assert(Any_Series(val1));

    if (val2) {
        assert(Any_Series(val2));

        if (Any_Array(val1)) {
            if (!Any_Array(val2))
                fail (Error_Unexpected_Type(VAL_TYPE(val1), VAL_TYPE(val2)));

            // As long as they're both arrays, we're willing to do:
            //
            //     >> union '(a b c) 'b/d/e
            //     (a b c d e)
            //
            // The type of the result will match the first value.
        }
        else if (Any_String(val1)) {

            // We will similarly do any two ANY-STRING? types:
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
    Series* out_ser;

    if (Any_Array(val1)) {
        Series* hser = 0;   // hash table for series
        Series* hret;       // hash table for return series

        // The buffer used for building the return series.  This creates
        // a new buffer every time, but reusing one might be slightly more
        // efficient.
        //
        Series* buffer = Make_Array(i);
        hret = Make_Hash_Series(i);   // allocated

        // Optimization note: !!
        // This code could be optimized for small blocks by not hashing them
        // and extending Find_Key to FIND on the value itself w/o the hash.

        do {
            // Note: val1 and val2 swapped 2nd pass!
            //
            const Array* array1 = Cell_Array(val1);

            // Check what is in series1 but not in series2
            //
            if (flags & SOP_FLAG_CHECK)
                hser = Hash_Block(val2, skip, cased);

            // Iterate over first series
            //
            i = VAL_INDEX(val1);
            for (; i < Array_Len(array1); i += skip) {
                const Element* item = Array_At(array1, i);
                if (flags & SOP_FLAG_CHECK) {
                    h = Find_Key_Hashed(
                        m_cast(Array*, Cell_Array(val2)),  // mode 1 unchanged
                        hser,
                        item,
                        skip,
                        cased,
                        1  // won't modify the input array
                    );
                    h = (h >= 0);
                    if (flags & SOP_FLAG_INVERT) h = !h;
                }
                if (h) {
                    Find_Key_Hashed(
                        cast(Array*, buffer),
                        hret,
                        item,
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
                Free_Unmanaged_Series(hser);

            if (not first_pass)
                break;
            first_pass = false;

            if ((flags & SOP_FLAG_BOTH) == 0)
                break;  // don't need to iterate over second series

            const Value* temp = val1;
            val1 = val2;
            val2 = temp;
        } while (true);

        if (hret)
            Free_Unmanaged_Series(hret);

        // The buffer may have been allocated too large, so copy it at the
        // used capacity size
        //
        out_ser = Copy_Array_Shallow(x_cast(Array*, buffer));
        Free_Unmanaged_Series(x_cast(Array*, buffer));
    }
    else if (Any_String(val1)) {
        DECLARE_MOLD (mo);

        // ask mo->series to have at least `i` capacity beyond mo->base.size
        //
        SET_MOLD_FLAG(mo, MOLD_FLAG_RESERVE);
        mo->reserve = i;
        Push_Mold(mo);

        do {
            // Note: val1 and val2 swapped 2nd pass!
            //
            const String* str = Cell_String(val1);

            DECLARE_ATOM (iter);
            Copy_Cell(iter, val1);

            // Iterate over first series
            //
            for (
                ;
                VAL_INDEX_RAW(iter) < cast(REBIDX, String_Len(str));
                VAL_INDEX_RAW(iter) += skip
            ){
                REBLEN len_match;

                if (flags & SOP_FLAG_CHECK) {
                    h = (NOT_FOUND != Find_Binstr_In_Binstr(
                        &len_match,
                        val2,
                        Cell_Series_Len_Head(val2),
                        iter,
                        1,  // single codepoint length
                        cased ? AM_FIND_CASE : 0,
                        skip
                    ));

                    if (flags & SOP_FLAG_INVERT) h = !h;
                }

                if (!h) continue;

                DECLARE_ATOM (mo_value);
                Reset_Unquoted_Header_Untracked(TRACK(mo_value), CELL_MASK_TEXT);
                Init_Cell_Node1(mo_value, mo->series);
                VAL_INDEX_RAW(mo_value) = mo->base.index;

                if (
                    NOT_FOUND == Find_Binstr_In_Binstr(
                        &len_match,
                        mo_value,
                        String_Len(mo->series),  // tail
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

            const Value* temp = val1;
            val1 = val2;
            val2 = temp;
        } while (true);

        out_ser = Pop_Molded_String(mo);
    }
    else {
        assert(Is_Binary(val1) and Is_Binary(val2));

        Binary* buf = BYTE_BUF;
        REBLEN buf_start_len = Binary_Len(buf);
        Expand_Series_Tail(buf, i);  // ask for at least `i` capacity
        REBLEN buf_at = buf_start_len;

        do {
            // Note: val1 and val2 swapped 2nd pass!
            //
            const Binary* bin = Cell_Binary(val1);

            // Iterate over first series
            //
            DECLARE_ATOM (iter);
            Copy_Cell(iter, val1);

            for (
                ;
                VAL_INDEX_RAW(iter) < cast(REBIDX, Binary_Len(bin));
                VAL_INDEX_RAW(iter) += skip
            ){
                REBLEN len_match;

                if (flags & SOP_FLAG_CHECK) {
                    h = (NOT_FOUND != Find_Binstr_In_Binstr(
                        &len_match,
                        val2,  // searched
                        Cell_Series_Len_Head(val2),  // limit (highest index)
                        iter,  // pattern
                        1,  // "part", e.g. matches only 1 byte
                        cased ? AM_FIND_CASE : 0,
                        skip
                    ));

                    if (flags & SOP_FLAG_INVERT) h = !h;
                }

                if (!h) continue;

                DECLARE_ATOM (buf_value);
                Reset_Unquoted_Header_Untracked(
                    TRACK(buf_value),
                    CELL_MASK_BINARY
                );
                Init_Cell_Node1(buf_value, buf);
                VAL_INDEX_RAW(buf_value) = buf_start_len;

                if (
                    NOT_FOUND == Find_Binstr_In_Binstr(
                        &len_match,
                        buf_value,  // searched
                        Cell_Series_Len_Head(buf_value),  // limit (highest index)
                        iter,  // pattern
                        1,  // "part", e.g. matches only 1 byte
                        cased ? AM_FIND_CASE : 0,  // flags
                        skip
                    )
                ){
                    Expand_Series_Tail(buf, skip);
                    Size size_at;
                    const Byte* iter_at = Cell_Binary_Size_At(&size_at, iter);
                    REBLEN min = MIN(size_at, skip);
                    memcpy(Binary_At(buf, buf_at), iter_at, min);
                    buf_at += min;
                }
            }

            if (not first_pass)
                break;
            first_pass = false;

            if ((flags & SOP_FLAG_BOTH) == 0)
                break;  // don't need to iterate over the second series

            const Value* temp = val1;
            val1 = val2;
            val2 = temp;
        } while (true);

        REBLEN out_len = buf_at - buf_start_len;
        Binary* out_bin = Make_Binary(out_len);
        memcpy(Binary_Head(out_bin), Binary_At(buf, buf_start_len), out_len);
        Term_Binary_Len(out_bin, out_len);
        out_ser = out_bin;

        Term_Binary_Len(buf, buf_start_len);
    }

    return out_ser;
}
