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
//  Compare_Bytes: C
//
// Compare two byte-wide strings. Return lexical difference.
//
// Uncase: compare is case-insensitive.
//
REBINT Compare_Bytes(const REBYTE *b1, const REBYTE *b2, REBLEN len, bool uncase)
{
    REBINT d;

    for (; len > 0; len--, b1++, b2++) {

        if (uncase) {
            //
            // !!! This routine is being possibly preserved for when faster
            // compare can be done on UTF-8 strings if the series caches if
            // all bytes are ASCII.  It is not meant to do "case-insensitive"
            // processing of binaries, however.
            //
            assert(*b1 < 0x80 and *b2 < 0x80);

            d = LO_CASE(*b1) - LO_CASE(*b2);
        }
        else
            d = *b1 - *b2;

        if (d != 0) return d;
    }

    return 0;
}


//
//  Match_Bytes: C
//
// Compare two binary strings. Return where the first differed.
// Case insensitive.
//
const REBYTE *Match_Bytes(const REBYTE *src, const REBYTE *pat)
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
//  Find_Bin_In_Bin: C
//
// Find an exact byte string within a byte string.
// Returns starting position or NOT_FOUND.
//
REBLEN Find_Bin_In_Bin(
    const REBSER *series,
    REBLEN offset,
    const REBYTE *bp2,
    REBSIZ size2,
    REBFLGS flags // AM_FIND_MATCH
){
    assert(SER_USED(series) >= offset);
    assert((flags & ~AM_FIND_MATCH) == 0);  // no AM_FIND_CASE

    if (size2 == 0 || (size2 + offset) > BIN_LEN(series))
        return NOT_FOUND; // pattern empty or is longer than the target

    const REBYTE *bp1 = BIN_AT(series, offset);
    REBLEN size1 = BIN_LEN(series) - offset;

    const REBYTE *end1
        = bp1 + ((flags & AM_FIND_MATCH) ? 1 : size1 - (size2 - 1));

    REBYTE b2 = *bp2; // first byte

    while (bp1 != end1) {
        if (*bp1 == b2) { // matched first byte
            REBLEN n;
            for (n = 1; n < size2; n++) {
                if (bp1[n] != bp2[n])
                    break;
            }
            if (n == size2)
                return bp1 - BIN_HEAD(series);
        }
        ++bp1;
    }

    return NOT_FOUND;
}


//
//  Find_Str_In_Bin: C
//
// Case-insensitive search for UTF-8 string within arbitrary BINARY! data.
// Returns starting position (as a byte index in the binary) or NOT_FOUND.
//
// Use caution with this function.  Not all byte patterns in a BINARY! are
// legal UTF-8, so this has to just kind of skip over any non-UTF-8 and
// consider it as "not a match".  But a match might be found in the middle of
// otherwise invalid UTF-8, so this might come as a surprise to some clients.
//
// NOTE: Series used must be > offset.
//
REBLEN Find_Str_In_Bin(
    const REBSER *series, // binary series to search in
    REBLEN offset, // where to begin search at
    const REBYTE *bp2, // pointer to UTF-8 data to search (guaranteed valid)
    REBLEN len2, // codepoint count of the UTF-8 data of interest
    REBSIZ size2, // encoded byte count of the UTF-8 data (not codepoints)
    REBFLGS flags // AM_FIND_MATCH, AM_FIND_CASE
){
    assert((flags & ~(AM_FIND_MATCH | AM_FIND_CASE)) == 0);

    // Due to the properties of UTF-8, a case-sensitive search on UTF-8 data
    // inside a binary can be done with plain Find_Bin_In_Bin().  It's faster.
    //
    if (flags & AM_FIND_CASE) {
        return Find_Bin_In_Bin(
            series,
            offset,
            bp2,
            size2,
            flags & AM_FIND_MATCH  // Bin_In_Bin asserts on AM_FIND_CASE
        );
    }

    if (size2 == 0 or (size2 + offset) > SER_USED(series))
        return NOT_FOUND; // pattern empty or is longer than the target

    const REBYTE *bp1 = BIN_AT(series, offset);
    REBSIZ size1 = BIN_LEN(series) - offset;

    const REBYTE *end1
        = bp1 + ((flags & AM_FIND_MATCH) ? 1 : size1 - (size2 - 1));

    REBUNI c2_canon; // first codepoint, but only calculate lowercase once
    REBCHR(const*) next2 = cast(REBCHR(const*), bp2);  // guaranteed valid
    next2 = NEXT_CHR(&c2_canon, next2);
    c2_canon = LO_CASE(c2_canon);

    while (bp1 < end1) {
        const REBYTE *next1;
        REBUNI c1;
        if (*bp1 == '\0') {  // 0 bytes can't match strings (no zero bytes)
            ++bp1;
            continue;
        }
        if (*bp1 < 0x80) {
            c1 = *bp1;
            next1 = bp1;
        }
        else {
            next1 = Back_Scan_UTF8_Char(&c1, bp1, NULL);
            if (next1 == NULL) {
                ++bp1;
                continue; // treat bad scans just as this byte not matching
            }
        }
        ++next1; // needed: see notes on why it's called "Back_Scan"

        if (LO_CASE(c1) == c2_canon) { // matched first char
            const REBYTE *temp1 = next1;
            REBCHR(const*) temp2 = next2;

            REBLEN n;
            for (n = 1; n < len2; n++) {
                if (*temp1 == '\0')  // 0 bytes can't match
                    break;
                else if (*temp1 < 0x80)
                    c1 = *temp1;
                else {
                    temp1 = Back_Scan_UTF8_Char(&c1, temp1, NULL);
                    if (temp1 == NULL)
                        break; // again, treat bad scans same as no match
                }
                ++temp1; // needed: see notes on why it's called "Back_Scan"

                REBUNI c2;
                temp2 = NEXT_CHR(&c2, temp2);

                if (LO_CASE(c1) != LO_CASE(c2))
                    break;
            }
            if (n == len2)
                return bp1 - BIN_HEAD(series);
        }

        bp1 = next1;
    }

    return NOT_FOUND;
}


//
//  Find_Str_In_Str: C
//
// General purpose find a substring.
//
// Supports: forward/reverse with skip, cased/uncase.
//
// Skip can be set positive or negative (for reverse).
//
// Flags are set according to ALL_FIND_REFS
//
REBLEN Find_Str_In_Str(
    const REBSTR *str1,
    REBLEN index_unsigned,
    REBLEN limit_unsigned,
    REBINT skip,
    REBCEL(const*) str2,
    REBLEN len,
    REBFLGS flags
){
    assert(index_unsigned <= STR_LEN(str1));

    assert((flags & ~(AM_FIND_CASE | AM_FIND_MATCH)) == 0);
    bool uncase = not (flags & AM_FIND_CASE);  // case insenstive

    // Signed quantities allow stepping outside of bounds (e.g. large /SKIP)
    // and still comparing...but incoming parameters should not be negative.
    //
    REBINT index = index_unsigned;
    REBINT end = limit_unsigned - len;

    // `str2` is always stepped through forwards in FIND, even with a negative
    // value for skip.  If the position is at the tail, it cannot be found.
    //
    REBCHR(const*) head2 = VAL_UTF8_AT(str2);
    if (*cast(const REBYTE*, head2) == '\0')
        return NOT_FOUND;  // getting c2 would be '\0' (LO_CASE illegal)

    REBUNI c2_canon;  // calculate first char lowercase once, vs. each step
    REBCHR(const*) next2 = NEXT_CHR(&c2_canon, head2);
    if (uncase)
        c2_canon = LO_CASE(c2_canon);

    // cp1 is the position in str1 that is our current tested head of a match
    //
    REBCHR(const*) cp1 = STR_AT(str1, index);

    REBUNI c1;  // c1 is the currently tested character for str1
    if (skip < 0) {
        //
        // Note: `find/skip tail "abcdef" "def" -3` is "def", so first search
        // position should be at the `d`.  We can reduce the amount of work
        // we do in the later loop checking against STR_LEN(str1) `len` by
        // up-front finding the earliest point we can look modulo `skip`,
        // e.g. `find/skip tail "abcdef" "cdef" -2` should start at `c`.
        //
        do {
            index += skip;
            if (index < 0)
                return NOT_FOUND;
            cp1 = SKIP_CHR(&c1, cp1, skip);
        } while (index + len > STR_LEN(str1));
    }
    else {
        if (index + len > STR_LEN(str1))
            return NOT_FOUND;

        c1 = CHR_CODE(cp1);
    }

    while (true) {
        if (c1 == c2_canon or (uncase and LO_CASE(c1) == c2_canon)) {
            //
            // The optimized first character match for str2 in str1 passed.
            // Now check subsequent positions, where both may need LO_CASE().
            //
            REBCHR(const*) tp1 = NEXT_STR(cp1);
            REBCHR(const*) tp2 = next2;  // next2 is second position in str2
            REBLEN n;
            for (n = 1; n < len; n++) {  // n = 0 (first char) already matched
                tp1 = NEXT_CHR(&c1, tp1);

                REBUNI c2;
                tp2 = NEXT_CHR(&c2, tp2);
                if (c1 == c2 or (uncase and LO_CASE(c1) == LO_CASE(c2)))
                    continue;  // the `for`

                break;  // the `for`
            }
            if (n == len)
                return index;
        }

        // The /MATCH flag historically indicates only considering the first
        // position, so exit loop on first mismatch.  (!!! Better name "/AT"?)
        //
        if (flags & AM_FIND_MATCH)
            goto not_found;

        index += skip;

        if (skip < 0) {
            if (index < 0)  // !!! What about /PART with negative skips?
                goto not_found;
            assert(cp1 >= STR_AT(str1, - skip));
        } else {
            if (index > end)
                goto not_found;
            assert(cp1 <= STR_AT(str1, STR_LEN(str1) - skip));
        }
        cp1 = SKIP_CHR(&c1, cp1, skip);
    }

  not_found:
    return NOT_FOUND;
}


//
//  Find_Char_In_Str: C
//
// Supports AM_FIND_CASE for case-sensitivity and AM_FIND_MATCH to check only
// the character at the current position and then stop.
//
// Skip can be set positive or negative (for reverse), and will be bounded
// by the `start` and `end`.
//
// Note that features like "/LAST" are handled at a higher level and
// translated into SKIP=(-1) and starting at (highest - 1).
//
REBLEN Find_Char_In_Str(
    REBUNI uni,         // character to look for
    const REBSTR *s,  // UTF-8 string series
    REBLEN index_orig,  // first index to examine (if out of range, NOT_FOUND)
    REBLEN highest,     // *one past* highest return result (e.g. SER_LEN)
    REBINT skip,        // step amount while searching, can be negative!
    REBFLGS flags       // AM_FIND_CASE, AM_FIND_MATCH
){
    assert((flags & ~(AM_FIND_CASE | AM_FIND_MATCH)) == 0);

    // !!! In UTF-8, finding a char in a string is really just like finding a
    // string in a string.  Optimize as this all folds together.

    if (uni == 0)
        return NOT_FOUND;  // there can be no `\0` codepoints in ANY-STRING!

    DECLARE_LOCAL (temp);
    Init_Char_May_Fail(temp, uni);

    REBLEN i = Find_Str_In_Str(
        s,
        index_orig,
        highest,
        skip,
        temp,
        1,  // `len`
        flags
    );
    Free_Unmanaged_Series(SER(temp));

    return i;
}


//
//  Find_Char_In_Bin: C
//
REBLEN Find_Char_In_Bin(
    REBUNI uni,         // character to look for
    const REBBIN *bin,  // binary series
    REBLEN lowest,      // lowest return index
    REBLEN index_orig,  // first index to examine (if out of range, NOT_FOUND)
    REBLEN highest,     // *one past* highest return result (e.g. SER_LEN)
    REBINT skip,        // step amount while searching, can be negative!
    REBFLGS flags       // AM_FIND_CASE, AM_FIND_MATCH
){
    assert((flags & ~(AM_FIND_CASE | AM_FIND_MATCH)) == 0);

    // !!! In UTF-8, finding a char in a string is really just like finding a
    // string in a string.  Optimize as this all folds together.

    if (skip != 1)
        fail ("Find_Char_In_Bin() does not support SKIP <> 1 at the moment");

    if (highest != BIN_LEN(bin))
        fail ("Find_Char_In_Bin() only searches the whole binary for now");

    UNUSED(lowest);

    if (uni == 0) {  // can't Make a REBSTR with 0 byte in it
        return Find_Bin_In_Bin(
            bin,
            index_orig,
            cb_cast(""),  // NUL terminator only
            1,  // 1 byte
            flags
        );
    }

    REBSTR *temp = Make_Codepoint_String(uni);

    REBLEN i = Find_Str_In_Bin(
        bin,
        index_orig,
        STR_HEAD(temp),
        1, // 1 character
        STR_SIZE(temp),
        flags
    );

    Free_Unmanaged_Series(SER(temp));

    return i;
}


//
//  Find_Bin_Bitset: C
//
// General purpose find a bitset char in a binary.
//
// Supports: forward/reverse with skip, cased/uncase, Unicode/byte.
//
// Skip can be set positive or negative (for reverse).
//
// Flags are set according to ALL_FIND_REFS
//
REBLEN Find_Bin_Bitset(
    const REBSER *bin,
    REBINT head,
    REBINT offset,
    REBINT tail,
    REBINT skip,
    const REBSER *bset,
    REBFLGS flags
){
    assert(head >= 0 && tail >= 0 && offset >= 0);

    assert((flags & ~AM_FIND_MATCH) == 0); // no AM_FIND_CASE

    const REBYTE *bp1 = BIN_AT(bin, offset);

    while (skip < 0 ? offset >= head : offset < tail) {
        const bool uncase = false;
        if (Check_Bit(bset, *bp1, uncase))
            return offset;

        if (flags & AM_FIND_MATCH)
            break;

        bp1 += skip;
        offset += skip;
    }

    return NOT_FOUND;

}


//
//  Find_Str_Bitset: C
//
// General purpose find a bitset char in a string.
//
// Supports: forward/reverse with skip, cased/uncase, Unicode/byte.
//
// Skip can be set positive or negative (for reverse).
//
// Flags are set according to ALL_FIND_REFS
//
REBLEN Find_Str_Bitset(
    const REBSTR *str,
    REBLEN index_unsigned,
    REBLEN end_unsigned,
    REBINT skip,
    const REBSER *bset,
    REBFLGS flags
){
    REBINT index = index_unsigned;
    REBINT end = end_unsigned;

    REBINT start;
    if (skip < 0) {
        start = 0;
    }
    else
        start = index;

    bool uncase = not (flags & AM_FIND_CASE); // case insensitive

    REBCHR(const*) cp1 = STR_AT(str, index);
    REBUNI c1;
    if (skip > 0)
        c1 = CHR_CODE(cp1);  // skip 1 will pass over cp1, so leave as is
    else
        cp1 = BACK_CHR(&c1, cp1);

    while (skip < 0 ? index >= start : index < end) {
        if (Check_Bit(bset, c1, uncase))
            return index;

        if (flags & AM_FIND_MATCH)
            break;

        cp1 = SKIP_CHR(&c1, cp1, skip);
        index += skip;
    }

    return NOT_FOUND;
}


//
//  Count_Lines: C
//
// Count lines in a UTF-8 file.
//
REBLEN Count_Lines(REBYTE *bp, REBLEN len)
{
    REBLEN count = 0;

    for (; len > 0; bp++, len--) {
        if (*bp == CR) {
            count++;
            if (len == 1) break;
            if (bp[1] == LF) bp++, len--;
        }
        else if (*bp == LF) count++;
    }

    return count;
}


//
//  Next_Line: C
//
// Find next line termination. Advance the bp; return bin length.
//
REBLEN Next_Line(REBYTE **bin)
{
    REBLEN count = 0;
    REBYTE *bp = *bin;

    for (; *bp; bp++) {
        if (*bp == CR) {
            bp++;
            if (*bp == LF) bp++;
            break;
        }
        else if (*bp == LF) {
            bp++;
            break;
        }
        else count++;
    }

    *bin = bp;
    return count;
}


//
//  Find_In_Any_Series: C
//
// !!! In R3-Alpha, the code for PARSE shared some of the same subroutines in
// %s-find.c as the FIND action.  However, there was still a lot of parallel
// logic in their invocation.  This is an attempt to further factor the common
// code, which hopefully will mean more consistency (as well as less code).
//
REBLEN Find_In_Any_Series(
    REBLEN *len,  // length of match (e.g. if pattern is a TAG!, includes <>)
    const RELVAL *any_series,  // VAL_INDEX() will be ignored...
    REBLEN index,  // ...overrides VAL_INDEX(any_series) for sliding searches
    REBCEL(const*) pattern,
    REBFLGS flags
){
    REBLEN end = VAL_LEN_HEAD(any_series);
    REBINT skip = 1;

    if (IS_BINARY(any_series)) {
        REBSIZ size;
        REBLEN result = find_binary(
            &size, VAL_SERIES(any_series), index, end, pattern, flags, skip
        );
        *len = size;
        return result;
    }

    if (ANY_STRING(any_series))
        return find_string(  // Note: returned len is in codepoints here
            len, VAL_STRING(any_series), index, end, pattern, flags, skip
        );

    fail ("Unknown series type for Find_In_Any_Series()");
}
