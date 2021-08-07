//
//  File: %s-find.c
//  Summary: "string search and comparison"
//  Section: strings
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
    const REBYTE *b1,
    const REBYTE *b2,
    REBLEN len
){
    for (; len > 0; len--, b1++, b2++) {
        assert(*b1 < 0x80 and *b2 < 0x80);

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
const REBYTE *Try_Diff_Bytes_Uncased(const REBYTE *src, const REBYTE *pat)
{
    while (*src != '\0' and *pat != '\0') {
        if (LO_CASE(*src++) != LO_CASE(*pat++))
            return 0;
    }

    if (*pat != '\0')
        return 0; // if not at end of pat, then error

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
REBINT Compare_UTF8(const REBYTE *s1, const REBYTE *s2, REBSIZ l2)
{
    REBUNI c1, c2;
    REBSIZ l1 = strsize(s1);
    REBINT result = 0;

    for (; l1 > 0 && l2 > 0; s1++, s2++, l1--, l2--) {
        c1 = *s1;
        c2 = *s2;
        if (c1 > 127) {
            s1 = Back_Scan_UTF8_Char(&c1, s1, &l1);
            assert(s1); // UTF8 should have already been verified good
        }
        if (c2 > 127) {
            s2 = Back_Scan_UTF8_Char(&c2, s2, &l2);
            assert(s2); // UTF8 should have already been verified good
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
// and forward/reverse (use negative skip for reverse).  Works with either
// UTF-8 or binary values by sensing the types of the cells.
//
// IMPORTANT: You can search for a string in a binary but searching for binary
// in string is *not* supported.  Such a search could match on a continuation
// byte, and there'd be no way to return that match measured as a codepoint
// position in the searched string (which is what FIND and PARSE require).
//
REBLEN Find_Binstr_In_Binstr(
    REBLEN *len_out,  // length in output units of match
    REBCEL(const*) binstr1,
    REBLEN end1_unsigned,  // end binstr1 *index* (not a limiting *length*)
    REBCEL(const*) binstr2,  // pattern to be found
    REBLEN limit2,  // in units of binstr2 (usually VAL_LEN_AT(binstr2))
    REBFLGS flags,  // AM_FIND_CASE, AM_FIND_MATCH
    REBINT skip1  // in length units of binstr1 (bytes or codepoints)
){
  #if !defined(NDEBUG)
    *len_out = 0xDECAFBAD;  // trash output length in case of no match
  #endif

    assert((flags & ~(AM_FIND_CASE | AM_FIND_MATCH)) == 0);

    bool is_2_str = (CELL_KIND(binstr2) != REB_BINARY);
    REBSIZ size2;
    REBLEN len2;
    const REBYTE* head2;
    if (IS_CHAR_CELL(binstr2) and VAL_CHAR(binstr2) == 0) {
        //
        // !!! Inelegant handling of `find #{00} #`, which should work, while
        // `find "" #` should not happen as NUL cannot exist in TEXT!, only
        // in BINARY!.
        //
        assert(CELL_KIND(binstr1) == REB_BINARY);
        head2 = cast(const REBYTE*, "\0");
        size2 = 1;
        if (limit2 < size2)
            size2 = limit2;
        len2 = 1;
        is_2_str = false;
    }
    else if (is_2_str) {
        head2 = VAL_UTF8_LEN_SIZE_AT_LIMIT(
            &len2,
            &size2,
            binstr2,
            limit2
        );
    }
    else {
        head2 = VAL_BINARY_SIZE_AT(&size2, binstr2);
        if (limit2 < size2)
            size2 = limit2;
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

    bool is_1_str = (CELL_KIND(binstr1) != REB_BINARY);
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

    // "`index` and `end` integrate the /PART.  If the /PART was negative,
    // then index would have been swapped to be the lower value...making what
    // was previously the index the limit.  However, that does not work with
    // negative `skip` values, which by default considers 0 the limit of the
    // backkwards search but otherwise presumably want a /PART to limit it.
    // Passing in a real "limit" vs. an end which could be greater or less
    // than the index would be one way of resolving this problem.  But it's
    // a missing feature for now to do FIND/SKIP/PART with a negative skip."
    //
    // !!! ^-- is this comment still relevant?
    //
    assert(end1_unsigned >= cast(REBLEN, index1));
    REBINT end1 = end1_unsigned - window1;

    // If is_2_str then we have to treat the data in binstr1 as characters,
    // even if it's not validated UTF-8.  This requires knowing the size_at
    // to pass to the checked version of Back_Scan_UTF8_Char().
    //
    const REBYTE *cp1;  // binstr1 position that is current test head of match
    REBLEN len_head1;
    REBSIZ size_at1;
    if (CELL_KIND(binstr1) == REB_ISSUE)  // no VAL_LEN_HEAD() atm
        cp1 = VAL_UTF8_LEN_SIZE_AT(&len_head1, &size_at1, binstr1);
    else if (CELL_KIND(binstr1) != REB_BINARY) {
        len_head1 = VAL_LEN_HEAD(binstr1);
        cp1 = VAL_UTF8_SIZE_AT(&size_at1, binstr1);
    }
    else {
        cp1 = VAL_BINARY_SIZE_AT(&size_at1, binstr1);
        len_head1 = VAL_LEN_HEAD(binstr1);
    }

    // The size of binary that can be used for checked UTF8 scans needs to
    // be reset each skip step.  If skipping right, the size needs to shrink
    // by the byte skip.  If skipping left, it needs to grow by the byte skip.
    // This is only applicable when treating a binstr1 binary as text.
    //
    REBSIZ size = size_at1;

    bool caseless = not (flags & AM_FIND_CASE);  // case insenstive
    if (not is_2_str)
        caseless = false;  //

    // Binary-compatible to: [next2 = NEXT_CHR(&c2_canon, head2)]
    REBUNI c2_canon;  // calculate first char lowercase once, vs. each step
    const REBYTE *next2;
    if (not is_2_str or *head2 < 0x80) {
        c2_canon = *head2;
        next2 = head2;
    } else
        next2 = Back_Scan_UTF8_Char_Unchecked(&c2_canon, head2);
    ++next2;

    if (caseless) {
        if (c2_canon == 0)
            assert(not is_1_str);
        else
            c2_canon = LO_CASE(c2_canon);
    }

    REBUNI c1;  // c1 is the currently tested character for str1
    if (skip1 < 0) {
        //
        // Note: `find/skip tail "abcdef" "def" -3` is "def", so first search
        // position should be at the `d`.  We can reduce the amount of work
        // we do in the later loop checking against STR_LEN(str1) `len` by
        // up-front finding the earliest point we can look modulo `skip`,
        // e.g. `find/skip tail "abcdef" "cdef" -2` should start at `c`.
        //
        do {
            index1 += skip1;
            if (index1 < 0)
                return NOT_FOUND;

            if (is_1_str)
                cp1 = SKIP_CHR(&c1, cast(REBCHR(const*), cp1), skip1);
            else if (is_2_str) {  // have to treat binstr1 as a string anyway
                cp1 += skip1;
                size -= skip1;  // size grows by skip
                const REBYTE* temp = Back_Scan_UTF8_Char(&c1, cp1, &size);
                if (temp == nullptr)
                    c1 = MAX_UNI + 1;  // won't match if `while` below breaks
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
            c1 = CHR_CODE(cast(REBCHR(const*), cp1));
        else if (is_2_str) {  // have to treat binstr1 as a string anyway
            REBSIZ size_temp = size;
            const REBYTE* temp = Back_Scan_UTF8_Char(&c1, cp1, &size_temp);
            if (temp == nullptr)
                goto no_match_at_this_position;
        }
        else
            c1 = *cp1;
    }

    while (true) {
        if (c1 == c2_canon or (caseless and c1 and LO_CASE(c1) == c2_canon)) {
            //
            // The optimized first character match for str2 in str1 passed.
            // Now check subsequent positions, where both may need LO_CASE().
            //

            // Binary-compatible to: [tp1 = NEXT_STR(cp1)]
            const REBYTE *tp1;
            if (is_1_str)  // binstr2 can't be binary
                tp1 = NEXT_STR(cast(REBCHR(const*), cp1));
            else if (is_2_str) {  // searching binary as if it's a string
                REBSIZ encoded_c1_size = Encoded_Size_For_Codepoint(c1);
                tp1 = cp1 + encoded_c1_size;
                size -= encoded_c1_size;
            }
            else
                tp1 = cp1 + 1;

            const REBYTE *tp2 = next2;  // next2 is second position in str2

            REBLEN n;
            for (n = 1; n < len2; n++) {  // n=0 (first item) already matched

                // Binary-compatible to: [tp1 = NEXT_CHR(&c1, tp1)]
                if (not is_2_str or *tp1 < 0x80)
                    c1 = *tp1;
                else if (is_1_str)
                    tp1 = Back_Scan_UTF8_Char_Unchecked(&c1, tp1);
                else {  // treating binstr1 as UTF-8 despite being binary
                    const REBYTE* temp = Back_Scan_UTF8_Char(&c1, tp1, &size);
                    if (temp == nullptr)  // invalid or incomplete UTF-8
                        goto no_match_at_this_position;
                    tp1 = temp;
                }
                ++tp1;

                // Binary-compatible to: [tp2 = NEXT_CHR(&c2, tp2)]
                REBUNI c2;
                if (not is_2_str or *tp2 < 0x80)
                    c2 = *tp2;
                else
                    tp2 = Back_Scan_UTF8_Char_Unchecked(&c2, tp2);
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
            if (index1 < 0)  // !!! What about /PART with negative skips?
                return NOT_FOUND;

            if (is_1_str)
                assert(cp1 >= STR_AT(VAL_STRING(binstr1), - skip1));
            else
                assert(cp1 >= BIN_AT(VAL_BINARY(binstr1), - skip1));
        } else {
            if (index1 > end1)
                return NOT_FOUND;

            if (is_1_str)
                assert(cp1 <= STR_AT(VAL_STRING(binstr1), len_head1 - skip1));
            else
                assert(cp1 <= BIN_AT(VAL_BINARY(binstr1), len_head1 - skip1));
        }

        // Regardless of whether we are searching in binstr1 as a string even
        // when it is a binary, the `skip` is in binstr1 units...so skip by
        // codepoints if string or bytes if not.
        //
        if (is_1_str)
            cp1 = SKIP_CHR(&c1, cast(REBCHR(const*), cp1), skip1);
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
// General purpose find a bitset char in a string or binary.
//
// Supports: forward/reverse with skip, cased/uncase, Unicode/byte.
//
// Skip can be set positive or negative (for reverse).
//
// Flags are set according to ALL_FIND_REFS
//
REBLEN Find_Bitset_In_Binstr(
    REBLEN *len_out,
    REBCEL(const*) binstr,
    REBLEN end_unsigned,
    REBINT skip,
    const REBBIN *bset,
    REBFLGS flags
){
  #if !defined(NDEBUG)
    *len_out = 0xDECAFBAD;
  #endif

    REBINT index = VAL_INDEX(binstr);
    REBINT end = end_unsigned;

    REBINT start;
    if (skip < 0)
        start = 0;
    else
        start = index;

    bool uncase = not (flags & AM_FIND_CASE); // case insensitive

    bool is_str = (CELL_KIND(binstr) != REB_BINARY);

    const REBYTE *cp1 = is_str ? VAL_STRING_AT(binstr) : VAL_BINARY_AT(binstr);
    REBUNI c1;
    if (skip > 0) {  // skip 1 will pass over cp1, so leave as is
        if (is_str)
            c1 = CHR_CODE(cast(REBCHR(const*), cp1));
        else
            c1 = *cp1;
    }
    else {
        if (is_str)
            cp1 = BACK_CHR(&c1, cast(REBCHR(const*), cp1));
        else {
            --cp1;
            c1 = *cp1;
        }
    }

    while (skip < 0 ? index >= start : index < end) {
        if (Check_Bit(bset, c1, uncase)) {
            //
            // !!! Now the output will always match 1 character or 1 byte.
            // If you were matching BINARY! in a mode that would match a
            // character codepoint, this length might be longer.  Review.
            //
            *len_out = 1;
            return index;
        }

        if (flags & AM_FIND_MATCH)
            break;

        if (is_str)
            cp1 = SKIP_CHR(&c1, cast(REBCHR(const*), cp1), skip);
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
// Service routine for both FIND and PARSE for searching in an ANY-STRING!,
// ISSUE!, or BINARY!
//
REBLEN Find_Value_In_Binstr(
    REBLEN *len,
    REBCEL(const*) binstr,
    REBLEN end,
    REBCEL(const*) pattern,
    REBLEN flags,
    REBINT skip
){
    enum Reb_Kind binstr_kind = CELL_KIND(binstr);
    enum Reb_Kind pattern_kind = CELL_KIND(pattern);

    if (REB_BINARY == pattern_kind) {
        //
        // Can't search for BINARY! in an ANY-STRING! (might match on a "half
        // codepoint").  Solution is to alias input as UTF-8 binary.
        //
        if (binstr_kind != REB_BINARY)
            fail (Error_Find_String_Binary_Raw());
        goto find_binstr_in_binstr;
    }

    if (
        ANY_STRING_KIND(pattern_kind)
        or ANY_WORD_KIND(pattern_kind)
        or REB_INTEGER == pattern_kind  // `find "ab10cd" 10` -> "10cd"
        or REB_ISSUE == pattern_kind
    ){
        if (binstr_kind != REB_BINARY and (
            IS_CHAR_CELL(pattern) and VAL_CHAR(pattern) == 0
        )){
            return NOT_FOUND;  // can't find NUL # in strings, only BINARY!
        }

      find_binstr_in_binstr: ;

        // !!! A TAG! does not have its delimiters in it.  The logic of the
        // find would have to be rewritten to accomodate this, and it's a
        // bit tricky as it is.  Let it settle down before trying that--and
        // for now just form the tag into a temporary alternate series.

        REBSTR *formed = nullptr;
        if (
            CELL_KIND(pattern) != REB_ISSUE
            and CELL_KIND(pattern) != REB_TEXT
            and CELL_KIND(pattern) != REB_BINARY
         ){
            // !!! `<tag>`, `set-word:` but FILE!, etc?
            //
            formed = Copy_Form_Cell(pattern, 0);
        }

        DECLARE_LOCAL (temp);  // !!! Note: unmanaged
        if (formed) {
            RESET_CELL(temp, REB_TEXT, CELL_FLAG_FIRST_IS_NODE);
            INIT_VAL_NODE1(temp, formed);
            PAYLOAD(Any, temp).second.u = 0;  // index
        }

        REBLEN result = Find_Binstr_In_Binstr(
            len,
            binstr,  // not all_ascii, has multibyte utf-8 sequences
            end,
            formed ? temp : pattern,
            UNLIMITED,
            flags & (AM_FIND_MATCH | AM_FIND_CASE),
            skip
        );

        if (formed)
            Free_Unmanaged_Series(formed);

        return result;
    }
    else if (pattern_kind == REB_BITSET) {
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
        fail ("Find_Value_In_Binstr() received unknown pattern datatype");
}
