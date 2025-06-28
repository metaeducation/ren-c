//
//  file: %s-find.c
//  summary: "string search and comparison"
//  section: strings
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

#include "sys-core.h"


//
//  Compare_Ascii_Uncased: C
//
// Variant of memcmp() that checks case-insensitively.  Just used to detect
// months in the scanner.  Returns a positive value, negative value, or 0.
// (Not clamped to [-1 0 1]!)
//
// !!! There have been suggestions that the system use the ISO date format,
// in order to be purely numeric and not need to vary by locale.  Review.
//
REBINT Compare_Ascii_Uncased(
    const Byte* b1,
    const Byte* b2,
    REBLEN len
){
    for (; len > 0; len--, b1++, b2++) {
        assert(Is_Byte_Ascii(*b1) and Is_Byte_Ascii(*b2));

        REBINT diff = LO_CASE(*b1) - LO_CASE(*b2);
        if (diff != 0)
            return diff;
    }
    return 0;
}


//
//  Try_Diff_Bytes_Uncased: C
//
// Compare two binary strings case insensitively, stopping at '\0' terminator.
// Return where the first differed.
//
Option(const Byte*) Try_Diff_Bytes_Uncased(const Byte* src, const Byte* pat)
{
    while (*src != '\0' and *pat != '\0') {
        if (LO_CASE(*src++) != LO_CASE(*pat++))
            return nullptr;
    }

    if (*pat != '\0')
        return nullptr;  // if not at end of pat, then error

    return src;
}


//
//  Compare_UTF8: C
//
// Compare two UTF8 strings.
//
// It is necessary to decode the strings to check if the match
// case-insensitively.
//
// Returns:
//     -3: no match, s2 > s1
//     -1: no match, s1 > s2
//      0: exact match
//      1: non-case match, s2 > s1
//      3: non-case match, s1 > s2
//
// So, result + 2 for no-match gives proper sort order.
// And, result - 2 for non-case match gives sort order.
//
// Used for: WORD comparison.
//
REBINT Compare_UTF8(const Byte* s1, const Byte* s2, Size l2)
{
    Codepoint c1, c2;
    Size l1 = strsize(s1);
    REBINT result = 0;

    for (; l1 > 0 && l2 > 0; s1++, s2++, l1--, l2--) {
        c1 = *s1;
        c2 = *s2;
        if (c1 > 127) {
            Option(Error*) e = Trap_Back_Scan_Utf8_Char(&c1, &s1, &l1);
            assert(not e);  // UTF8 good, use Back_Scan_Utf8_Char_Unchecked()!
            UNUSED(e);
        }
        if (c2 > 127) {
            Option(Error*) e = Trap_Back_Scan_Utf8_Char(&c2, &s2, &l2);
            assert(not e);  // UTF8 good, use Back_Scan_Utf8_Char_Unchecked()!
            UNUSED(e);
        }
        if (c1 != c2) {
            if (LO_CASE(c1) != LO_CASE(c2))
                return (c1 > c2) ? -1 : -3;

            if (result == 0)
                result = (c1 > c2) ? 3 : 1;
        }
    }

    if (l1 != l2)
        result = (l1 > l2) ? -1 : -3;

    return result;
}


//
//  Find_Binstr_In_Binstr: C
//
// General purpose find a substring.  Supports cased and uncased searches,
// and forward vs. reverse (use negative skip for reverse).  Works with either
// UTF-8 or binary values by sensing the types of the cells.
//
// IMPORTANT: You can search for a string in a binary but searching for binary
// in string is *not* supported.  Such a search could match on a continuation
// byte, and there'd be no way to return that match measured as a codepoint
// position in the searched string (which is what FIND and PARSE require).
//
// 1. `limit2` is typically Series_Len_At(binstr2))
//
REBINT Find_Binstr_In_Binstr(
    Sink(REBLEN) len_out,  // length in output units of match
    const Value* binstr1,
    REBLEN end1,  // end binstr1 *index* (not a limiting *length*)
    const Value* binstr2,  // pattern to be found
    Option(const Length*) limit2,  // in units of binstr2 [1]
    Flags flags,  // AM_FIND_CASE, AM_FIND_MATCH
    REBINT skip1  // in length units of binstr1 (bytes or codepoints)
){
    assert((flags & ~(AM_FIND_CASE | AM_FIND_MATCH)) == 0);

    bool is_2_str = (Heart_Of(binstr2) != TYPE_BLOB);
    Size size2;
    Length len2;
    const Byte* head2;
    if (is_2_str) {
        head2 = Cell_Utf8_Len_Size_At_Limit(
            &len2,
            &size2,
            binstr2,
            limit2
        );
    }
    else {
        head2 = Blob_Size_At(&size2, binstr2);
        if (limit2 and *(unwrap limit2) < size2)
            size2 = *(unwrap limit2);
        len2 = size2;
    }

    // `str2` is always stepped through forwards in FIND, even with a negative
    // value for skip.  If the position is at the tail, it is considered
    // to be found, e.g. `find "abc" ""` is "abc"...there are infinitely many
    // empty strings at each string position.
    //
    if (len2 == 0) {
        assert(size2 == 0); // Note: c2 at end of '\0' means LO_CASE illegal
        *len_out = 0;
        return VAL_INDEX(binstr1);
    }

    bool is_1_str = (Heart_Of(binstr1) != TYPE_BLOB);
    assert(not (is_1_str and not is_2_str));  // see `IMPORTANT` comment above

    // The search window size in units of binstr1.  It's the length or size of
    // the search pattern...and it's the size in bytes for the only allowed
    // mismatch case (where binstr1 is binary and binstr2 is string)
    //
    REBLEN window1 = is_1_str ? len2 : size2;

    // Signed quantities allow stepping outside of bounds (e.g. large /SKIP)
    // and still comparing...but incoming parameters should not be negative.
    //
    REBINT index1 = VAL_INDEX(binstr1);

    // "`index` and `end` integrate the :PART.  If the :PART was negative,
    // then index would have been swapped to be the lower value...making what
    // was previously the index the limit.  However, that does not work with
    // negative `skip` values, which by default considers 0 the limit of the
    // backkwards search but otherwise presumably want a :PART to limit it.
    // Passing in a real "limit" vs. an end which could be greater or less
    // than the index would be one way of resolving this problem.  But it's
    // a missing feature for now to do FIND:SKIP:PART with a negative skip."
    //
    // !!! ^-- is this comment still relevant?
    //
    end1 = end1 - window1;

    // If is_2_str then we have to treat the data in binstr1 as characters,
    // even if it's not validated UTF-8.  This requires knowing the size_at
    // to pass to the checked version of Trap_Back_Scan_Utf8_Char().
    //
    const Byte* cp1;  // binstr1 position that is current test head of match
    Length len_head1;
    Size size_at1;
    if (Heart_Of(binstr1) == TYPE_BLOB) {
        cp1 = Blob_Size_At(&size_at1, binstr1);
        len_head1 = Series_Len_Head(binstr1);
    }
    else {
        len_head1 = Series_Len_Head(binstr1);
        cp1 = Cell_Utf8_Size_At(&size_at1, binstr1);
    }

    // The size of binary that can be used for checked UTF8 scans needs to
    // be reset each skip step.  If skipping right, the size needs to shrink
    // by the byte skip.  If skipping left, it needs to grow by the byte skip.
    // This is only applicable when treating a binstr1 binary as text.
    //
    Size size = size_at1;

    bool caseless = not (flags & AM_FIND_CASE);  // case insenstive
    if (not is_2_str)
        caseless = false;  //

    // Binary-compatible to: [next2 = Utf8_Next(&c2_canon, head2)]
    Codepoint c2_canon;  // calculate first char lowercase once, vs. each step
    const Byte* next2;
    if (not is_2_str or Is_Byte_Ascii(*head2)) {
        c2_canon = *head2;
        next2 = head2;
    } else
        next2 = Back_Scan_Utf8_Char_Unchecked(&c2_canon, head2);
    ++next2;

    if (caseless) {
        if (c2_canon == 0)
            assert(not is_1_str);
        else
            c2_canon = LO_CASE(c2_canon);
    }

    Codepoint c1;  // c1 is the currently tested character for str1
    if (skip1 < 0) {
        //
        // Note: `find:skip tail "abcdef" "def" -3` is "def", so first search
        // position should be at the `d`.  We can reduce the amount of work
        // we do in the later loop checking against Strand_Len(str1) `len` by
        // up-front finding the earliest point we can look modulo `skip`,
        // e.g. `find:skip tail "abcdef" "cdef" -2` should start at `c`.
        //
        do {
            index1 += skip1;
            if (index1 < 0)
                return NOT_FOUND;

            if (is_1_str)
                cp1 = Utf8_Skip(&c1, cast(Utf8(const*), cp1), skip1);
            else if (is_2_str) {  // have to treat binstr1 as a string anyway
                cp1 += skip1;
                size -= skip1;  // size grows by skip
                Option(Error*) e = Trap_Back_Scan_Utf8_Char(&c1, &cp1, &size);
                if (e) {
                    UNUSED(e);  // UTF-8 errors preallocated, cheap to ignore!
                    c1 = MAX_UNI + 1;  // won't match if `while` below breaks
                }
            }
            else {  // treat binstr1 as the binary that it is
                cp1 += skip1;
                c1 = *cp1;
            }
        } while (index1 + window1 > len_head1);
    }
    else {
        if (index1 + window1 > len_head1)
            return NOT_FOUND;

        if (is_1_str)
            c1 = Codepoint_At(cast(Utf8(const*), cp1));
        else if (is_2_str) {  // have to treat binstr1 as a string anyway
            Size size_temp = size;
            Option(Error*) e = Trap_Back_Scan_Utf8_Char(&c1, &cp1, &size_temp);
            if (e) {
                UNUSED(e);  // UTF-8 errors preallocated, cheap to ignore!
                goto no_match_at_this_position;
            }
        }
        else
            c1 = *cp1;
    }

    while (true) {
        //
        // !!! NOTE: This is broken, because c1 is a byte in the searched
        // binary, and c2_canon is a UTF-8 character.  This creaky old routine
        // needs a full rewrite to fix it and make it more readable, in light
        // of modern understandings, when time permits.
        //
        if (c1 == c2_canon or (caseless and c1 and LO_CASE(c1) == c2_canon)) {
            //
            // The optimized first character match for str2 in str1 passed.
            // Now check subsequent positions, where both may need LO_CASE().
            //

            // Binary-compatible to: [tp1 = Skip_Codepoint(cp1)]
            const Byte* tp1;
            if (is_1_str)  // binstr2 can't be binary
                tp1 = Skip_Codepoint(cast(Utf8(const*), cp1));
            else if (is_2_str) {  // searching binary as if it's a string
                Size encoded_c1_size = Encoded_Size_For_Codepoint(c1);
                tp1 = cp1 + encoded_c1_size;
                size -= encoded_c1_size;
            }
            else
                tp1 = cp1 + 1;

            const Byte* tp2 = next2;  // next2 is second position in str2

            REBLEN n;
            for (n = 1; n < len2; n++) {  // n=0 (first item) already matched

                // Binary-compatible to: [tp1 = Utf8_Next(&c1, tp1)]
                if (not is_2_str or *tp1 < 0x80)
                    c1 = *tp1;
                else if (is_1_str)
                    tp1 = Back_Scan_Utf8_Char_Unchecked(&c1, tp1);
                else {  // treating binstr1 as UTF-8 despite being binary
                    Option(Error*) e = Trap_Back_Scan_Utf8_Char(
                        &c1, &tp1, &size
                    );
                    if (e) {
                        UNUSED(e);  // UTF-8 errors prealloc, cheap to ignore!
                        goto no_match_at_this_position;
                    }
                }
                ++tp1;

                // Binary-compatible to: [tp2 = Utf8_Next(&c2, tp2)]
                Codepoint c2;
                if (not is_2_str or *tp2 < 0x80)
                    c2 = *tp2;
                else
                    tp2 = Back_Scan_Utf8_Char_Unchecked(&c2, tp2);
                ++tp2;

                if (c1 == c2)
                    continue;

                if (caseless and LO_CASE(c1) == LO_CASE(c2))
                    continue;

                goto no_match_at_this_position;
            }
            if (n == len2) {
                *len_out = window1;
                return index1;
            }
        }

      no_match_at_this_position:

        // The /MATCH flag historically indicates only considering the first
        // position, so exit loop on first mismatch.  (!!! Better name "/AT"?)
        //
        if (flags & AM_FIND_MATCH)
            return NOT_FOUND;

        index1 += skip1;

        if (skip1 < 0) {
            if (index1 < 0)  // !!! What about :PART with negative skips?
                return NOT_FOUND;

            if (is_1_str)
                assert(cp1 >= Strand_At(Cell_Strand(binstr1), - skip1));
            else
                assert(cp1 >= Binary_At(Cell_Binary(binstr1), - skip1));
        } else {
            if (index1 > end1)
                return NOT_FOUND;

            if (is_1_str)
                assert(cp1 <= Strand_At(Cell_Strand(binstr1), len_head1 - skip1));
            else
                assert(cp1 <= Binary_At(Cell_Binary(binstr1), len_head1 - skip1));
        }

        // Regardless of whether we are searching in binstr1 as a string even
        // when it is a binary, the `skip` is in binstr1 units...so skip by
        // codepoints if string or bytes if not.
        //
        if (is_1_str)
            cp1 = Utf8_Skip(&c1, cast(Utf8(const*), cp1), skip1);
        else {
            // When binstr2 is a string and binstr1 isn't, we are treating
            // binstr1 as a string despite being unchecked bytes.  Reset the
            // size bound for doing the character skcanning.
            //
            if (is_2_str)
                size = size_at1 - skip1;

            cp1 += skip1;
            c1 = *cp1;
        }
    }

    return NOT_FOUND;
}


//
//  Find_Bitset_In_Binstr: C
//
// General purpose find a bitset char in a string or binary.  Returns NOT_FOUND
// (-1) on failure.
//
// Supports: forward or reverse with skip, cased or uncased, Unicode or bytes.
//
// Skip can be set positive or negative (for reverse).
//
// Flags are set according to ALL_FIND_REFS
//
REBINT Find_Bitset_In_Binstr(
    Sink(Length) len_out,
    const Value* binstr,
    REBLEN end_unsigned,
    REBINT skip,
    const Binary* bset,
    Flags flags
){
    REBINT index = VAL_INDEX(binstr);
    REBINT end = end_unsigned;

    REBINT start;
    if (skip < 0)
        start = 0;
    else
        start = index;

    bool uncase = not (flags & AM_FIND_CASE); // case insensitive

    bool is_str = (Heart_Of(binstr) != TYPE_BLOB);

    const Byte* cp1 = is_str ? String_At(binstr) : Blob_At(binstr);
    Codepoint c1;
    if (skip > 0) {  // skip 1 will pass over cp1, so leave as is
        if (is_str)
            c1 = Codepoint_At(cast(Utf8(const*), cp1));
        else
            c1 = *cp1;
    }
    else {
        if (is_str)
            cp1 = Utf8_Back(&c1, cast(Utf8(const*), cp1));
        else {
            --cp1;
            c1 = *cp1;
        }
    }

    while (skip < 0 ? index >= start : index < end) {
        if (Check_Bit(bset, c1, uncase)) {
            //
            // !!! Now the output will always match 1 character or 1 byte.
            // If you were matching BLOB! in a mode that would match a
            // character codepoint, this length might be longer.  Review.
            //
            *len_out = 1;
            return index;
        }

        if (flags & AM_FIND_MATCH)
            break;

        if (is_str)
            cp1 = Utf8_Skip(&c1, cast(Utf8(const*), cp1), skip);
        else {
            cp1 += skip;
            c1 = *cp1;
        }
        index += skip;
    }

    return NOT_FOUND;
}


//
//  Find_Value_In_Binstr: C
//
// Service routine for both FIND and PARSE for searching in an ANY-STRING?,
// RUNE!, or BLOB!
//
REBLEN Find_Value_In_Binstr(
    REBLEN *len,
    const Element* binstr,
    REBLEN end,
    const Element* pattern,
    REBLEN flags,
    REBINT skip
){
    Heart binstr_heart = Heart_Of_Builtin_Fundamental(binstr);
    Heart pattern_heart = Heart_Of_Builtin(pattern);
    Count pattern_quotes = Quotes_Of(pattern);

    if (TYPE_BLOB == pattern_heart and binstr_heart != TYPE_BLOB) {
        //
        // Can't search for BLOB! in an ANY-STRING? (might match on a "half
        // codepoint").  Solution is to alias input as UTF-8 binary.
        //
        if (binstr_heart != TYPE_BLOB)
            panic (Error_Find_String_Binary_Raw());
    }

    if (
        pattern_quotes == 1
        or (pattern_quotes == 0 and (
            Any_Utf8_Type(pattern_heart)
            or TYPE_INTEGER == pattern_heart  // `find "ab10cd" 10` -> "10cd"
            or TYPE_BLOB == pattern_heart  // binstr_heart checked for TYPE_BLOB
        ))
    ){
        if (binstr_heart != TYPE_BLOB and Is_Blob_And_Is_Zero(pattern))
            return NOT_FOUND;  // can't find NUL # in strings, only BLOB!

        // FIND provides the basis for matching things literally in strings
        // via quoted items, while the baseline behavior for finding things
        // like tags does not consider the delimiters:
        //
        //   >> find "ab<c>d" <c>
        //   == "c>d"
        //
        //   >> find "ab<c>d" quote <c>
        //   == "<c>d"

        Strand* molded = nullptr;
        if (pattern_quotes == 1 or Heart_Of(pattern) == TYPE_INTEGER)
            molded = Copy_Mold_Cell_Ignore_Quotes(pattern, 0);

        DECLARE_ELEMENT (temp);  // !!! Note: unmanaged
        if (molded) {
            Reset_Cell_Header_Noquote(temp, CELL_MASK_TEXT);
            CELL_PAYLOAD_1(temp) = molded;
            VAL_INDEX_RAW(temp) = 0;
        }

        REBLEN result = Find_Binstr_In_Binstr(
            len,
            binstr,  // not all ascii, has multibyte utf-8 sequences
            end,
            molded ? temp : pattern,
            UNLIMITED,
            flags & (AM_FIND_MATCH | AM_FIND_CASE),
            skip
        );

        if (molded)
            Free_Unmanaged_Flex(molded);

        return result;
    }
    else if (
        LIFT_BYTE(pattern) == NOQUOTE_2
        and pattern_heart == TYPE_BITSET
    ){
        return Find_Bitset_In_Binstr(
            len,
            binstr,
            end,
            skip,
            VAL_BITSET(pattern),
            flags & (AM_FIND_MATCH | AM_FIND_CASE)
        );
    }
    else
        panic ("Find_Value_In_Binstr() received unknown pattern datatype");
}
