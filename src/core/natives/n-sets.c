//
//  file: %n-sets.c
//  summary: "native functions for data sets"
//  section: natives
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
// Making things more complex was the introduction of a :SKIP parameter, which
// had a somewhat dubious definition of treating the series as fixed-length
// spans where the set operation was based on the first element of that span.
//
//     rebol2>> exclude:skip [a b c d] [c] 2
//     == [a b]
//
// The operations are kept here mostly in their R3-Alpha form, though they
// had to be adapted to deal with the difference between UTF-8 strings and
// binaries.
//

#include "sys-core.h"


//
//  Make_Set_Operation_Flex: C
//
// Do set operations on a Flex.  Case-sensitive if `cased` is TRUE.
// `skip` is the record size.
//
Flex* Make_Set_Operation_Flex(
    const Value* val1,
    const Value* val2,
    Flags flags,
    bool cased,
    REBLEN skip
){
    assert(Any_Series(val1));
    Type type1 = unwrap Type_Of(val1);

    if (val2) {
        assert(Any_Series(val2));

        if (Any_List(val1)) {
            if (!Any_List(val2))
                panic (Error_Unexpected_Type(type1, Datatype_Of(val2)));

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
                panic (Error_Unexpected_Type(type1, Datatype_Of(val2)));
        }
        else {
            // Binaries only operate with other binaries
            assert(Is_Blob(val1));
            if (not Is_Blob(val2))
                panic (Error_Unexpected_Type(type1, Datatype_Of(val2)));
        }
    }

    // Calculate `i` as maximum length of result block.  The temporary buffer
    // will be allocated at this size, but copied out at the exact size of
    // the actual result.
    //
    REBLEN i = Series_Len_At(val1);
    if (flags & SOP_FLAG_BOTH)
        i += Series_Len_At(val2);

    REBINT h = 1; // used for both logic true/false and hash check
    bool first_pass = true; // are we in the first pass over the series?
    Flex* out_flex;

    if (Any_List(val1)) {
        HashList* hflex = 0;   // hash table for series
        HashList* hret;       // hash table for return series

        // The buffer used for building the return series.  This creates
        // a new buffer every time, but reusing one might be slightly more
        // efficient.
        //
        Array* buffer = Make_Source(i);
        hret = Make_Hashlist(i);   // allocated

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
                hflex = Hash_Block(val2, skip, cased);

            // Iterate over first series
            //
            i = Series_Index(val1);
            for (; i < Array_Len(array1); i += skip) {
                const Element* item = Array_At(array1, i);
                if (flags & SOP_FLAG_CHECK) {
                    h = Find_Key_Hashed(
                        m_cast(Source*, Cell_Array(val2)),  // mode 1 unchanged
                        hflex,
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
                        buffer,
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
                // with things like (intersect:skip [1 2 3] [7] 2) is too
                // shaky to deal with, so an error is reported if it does
                // not work out evenly to the skip size.
                //
                panic (Error_Block_Skip_Wrong_Raw());
            }

            if (flags & SOP_FLAG_CHECK)
                Free_Unmanaged_Flex(hflex);

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
            Free_Unmanaged_Flex(hret);

        // The buffer may have been allocated too large, so copy it at the
        // used capacity size
        //
        out_flex = Copy_Array_Shallow(m_cast(Array*, buffer));
        Free_Unmanaged_Flex(m_cast(Array*, buffer));
    }
    else if (Any_String(val1)) {
        DECLARE_MOLDER (mo);

        // ask mo->strand to have at least `i` capacity beyond mo->base.size
        //
        SET_MOLD_FLAG(mo, MOLD_FLAG_RESERVE);
        mo->reserve = i;
        Push_Mold(mo);

        do {
            // Note: val1 and val2 swapped 2nd pass!
            //
            const Strand* str = Cell_Strand(val1);

            DECLARE_VALUE (iter);
            Copy_Cell(iter, val1);

            // Iterate over first series
            //
            for (
                ;
                SERIES_INDEX_UNBOUNDED(iter) < Strand_Len(str);
                SERIES_INDEX_UNBOUNDED(iter) += skip
            ){
                REBLEN len_match;

                Length single_codepoint_len = 1;  // Length, not size in bytes

                if (flags & SOP_FLAG_CHECK) {
                    h = (NOT_FOUND != Find_Binstr_In_Binstr(
                        &len_match,
                        val2,
                        Series_Len_Head(val2),
                        iter,
                        &single_codepoint_len,  // "part as 1 codepoint
                        cased ? AM_FIND_CASE : 0,
                        skip
                    ));

                    if (flags & SOP_FLAG_INVERT) h = !h;
                }

                if (!h) continue;

                DECLARE_ELEMENT (mo_value);
                Reset_Cell_Header_Noquote(TRACK(mo_value), CELL_MASK_TEXT);
                CELL_PAYLOAD_1(mo_value) = mo->strand;
                SERIES_INDEX_UNBOUNDED(mo_value) = mo->base.index;

                if (
                    NOT_FOUND == Find_Binstr_In_Binstr(
                        &len_match,
                        mo_value,
                        Strand_Len(mo->strand),  // tail
                        iter,
                        &single_codepoint_len,  // "part" as one codepoint
                        cased ? AM_FIND_CASE : 0,  // flags
                        skip  // skip
                    )
                ){
                    Append_Any_Utf8_Limit(mo->strand, iter, &skip);
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

        out_flex = Pop_Molded_Strand(mo);
    }
    else {
        assert(Is_Blob(val1) and Is_Blob(val2));

        Binary* buf = BYTE_BUF;
        REBLEN buf_start_len = Binary_Len(buf);
        Expand_Flex_Tail(buf, i);  // ask for at least `i` capacity
        REBLEN buf_at = buf_start_len;

        do {
            // Note: val1 and val2 swapped 2nd pass!
            //
            const Binary* b = Cell_Binary(val1);

            // Iterate over first series
            //
            DECLARE_VALUE (iter);
            Copy_Cell(iter, val1);

            for (
                ;
                SERIES_INDEX_UNBOUNDED(iter) < Binary_Len(b);
                SERIES_INDEX_UNBOUNDED(iter) += skip
            ){
                REBLEN len_match;

                Length single_byte_len = 1;

                if (flags & SOP_FLAG_CHECK) {
                    h = (NOT_FOUND != Find_Binstr_In_Binstr(
                        &len_match,
                        val2,  // searched
                        Series_Len_Head(val2),  // limit (highest index)
                        iter,  // pattern
                        &single_byte_len,  // "part" as one byte
                        cased ? AM_FIND_CASE : 0,
                        skip
                    ));

                    if (flags & SOP_FLAG_INVERT) h = !h;
                }

                if (!h) continue;

                DECLARE_ELEMENT (buf_value);
                Reset_Cell_Header_Noquote(
                    TRACK(buf_value),
                    CELL_MASK_BLOB
                );
                SERIESLIKE_PAYLOAD_1_BASE(buf_value) = buf;
                SERIESLIKE_PAYLOAD_2_INDEX(buf_value) = buf_start_len;

                if (
                    NOT_FOUND == Find_Binstr_In_Binstr(
                        &len_match,
                        buf_value,  // searched
                        Series_Len_Head(buf_value),  // limit (highest index)
                        iter,  // pattern
                        &single_byte_len,  // "part" as one byte
                        cased ? AM_FIND_CASE : 0,  // flags
                        skip
                    )
                ){
                    Expand_Flex_Tail(buf, skip);
                    Size size_at;
                    const Byte* iter_at = Blob_Size_At(&size_at, iter);
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
        out_flex = out_bin;

        Term_Binary_Len(buf, buf_start_len);
    }

    return out_flex;
}


//
//  complement: native:generic [
//
//  "Returns the inversion of a set"
//
//      return: [bitset!]
//      value [bitset!]
//  ]
//
DECLARE_NATIVE(COMPLEMENT)
{
    Element* e = cast(Element*, ARG_N(1));
    return Dispatch_Generic(COMPLEMENT, e, LEVEL);
}


//
//  intersect: native:generic [
//
//  "Returns the intersection (AND) of two sets"
//
//      return: [
//          integer! char? tuple!  ; math
//          any-list? any-string? bitset!  ; sets
//          blob!  ; ???
//      ]
//      value1 [
//          integer! char? tuple!  ; math
//          any-list? any-string? bitset!  ; sets
//          blob!  ; ???
//      ]
//      value2 [
//          integer! char? tuple!  ; math
//          any-list? any-string? bitset!  ; sets
//          blob!  ; ???
//      ]
//      :case "Uses case-sensitive comparison"
//      :skip "Treat the series as records of fixed size"
//          [integer!]
//  ]
//
DECLARE_NATIVE(INTERSECT)
{
    Element* e1 = cast(Element*, ARG_N(1));
    return Dispatch_Generic(INTERSECT, e1, LEVEL);
}


//
//  union: native:generic [
//
//  "Returns the union (OR) of two sets"
//
//      return: [
//          integer! char? tuple!  ; math
//          any-list? any-string? bitset!  ; sets
//          blob!  ; ???
//      ]
//      value1 [
//          integer! char? tuple!  ; math
//          any-list? any-string? bitset!  ; sets
//          blob!  ; ???
//      ]
//      value2 [
//          integer! char? tuple!  ; math
//          any-list? any-string? bitset!  ; sets
//          blob!  ; ???
//      ]
//      :case "Use case-sensitive comparison"
//      :skip "Treat the series as records of fixed size"
//          [integer!]
//  ]
//
DECLARE_NATIVE(UNION)
{
    Element* e1 = cast(Element*, ARG_N(1));
    return Dispatch_Generic(UNION, e1, LEVEL);
}


//
//  difference: native:generic [
//
//  "Returns the special difference (XOR) of two sets"
//
//      return: [
//          integer! char? tuple!
//          any-list? any-string? bitset!
//          blob!
//          time!  ; !!! Under review, this really doesn't fit
//      ]
//      value1 [
//          integer! char? tuple!  ; math
//          any-list? any-string? bitset!  ; sets
//          blob!  ; ???
//          date!  ; !!! Under review, this really doesn't fit
//      ]
//      value2 [
//          integer! char? tuple!  ; math
//          any-list? any-string? bitset!  ; sets
//          blob!  ; ???
//          date!  ; !!! Under review, this really doesn't fit
//      ]
//      :case "Uses case-sensitive comparison"
//      :skip "Treat the series as records of fixed size"
//          [integer!]
//  ]
//
DECLARE_NATIVE(DIFFERENCE)
{
    Element* e1 = cast(Element*, ARG_N(1));
    return Dispatch_Generic(DIFFERENCE, e1, LEVEL);
}


//
//  exclude: native:generic [
//
//  "Returns the first data set less the second data set"
//
//      return: [any-list? any-string? blob! bitset!]
//      data "original data"
//          [any-list? any-string? blob! bitset!]
//      exclusions "data to exclude from series"
//          [any-list? any-string? blob! bitset!]
//      :case "Uses case-sensitive comparison"
//      :skip "Treat the series as records of fixed size"
//          [integer!]
//  ]
//
DECLARE_NATIVE(EXCLUDE)
{
    Element* e1 = cast(Element*, ARG_N(1));
    return Dispatch_Generic(EXCLUDE, e1, LEVEL);
}


//
//  unique: native:generic [
//
//  "Returns the data set with duplicates removed"
//
//      return: [any-list? any-string? blob! bitset!]
//      series [any-list? any-string? blob! bitset!]
//      :case "Use case-sensitive comparison (except bitsets)"
//      :skip "Treat the series as records of fixed size"
//          [integer!]
//  ]
//
DECLARE_NATIVE(UNIQUE)
{
    Element* e1 = cast(Element*, ARG_N(1));
    return Dispatch_Generic(UNIQUE, e1, LEVEL);
}
