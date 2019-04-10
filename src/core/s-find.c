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
//=////////////////////////////////////////////////////////////////////////=//
//

#include "sys-core.h"


//
//  Compare_Binary_Vals: C
//
// Compares bytes, not chars. Return the difference.
//
REBINT Compare_Binary_Vals(const REBCEL *v1, const REBCEL *v2)
{
    REBCNT l1 = VAL_LEN_AT(v1);
    REBCNT l2 = VAL_LEN_AT(v2);
    REBCNT len = MIN(l1, l2);
    
    REBINT n = memcmp(
        SER_AT_RAW(SER_WIDE(VAL_SERIES(v1)), VAL_SERIES(v1), VAL_INDEX(v1)),
        SER_AT_RAW(SER_WIDE(VAL_SERIES(v2)), VAL_SERIES(v2), VAL_INDEX(v2)),
        len
    );

    if (n != 0) return n;

    return l1 - l2;
}


//
//  Compare_Bytes: C
//
// Compare two byte-wide strings. Return lexical difference.
//
// Uncase: compare is case-insensitive.
//
REBINT Compare_Bytes(const REBYTE *b1, const REBYTE *b2, REBCNT len, bool uncase)
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
//  Match_Sub_Path: C
//
// Compare two file path series, regardless of char size.
// Return true if s1 is a subpath of s2.
// Case insensitive.
//
bool Match_Sub_Path(REBSTR *s1, REBSTR *s2)
{
    REBCNT len1 = STR_LEN(s1);

    if (len1 > STR_LEN(s2))
        return false;

    REBUNI c1 = 0;
    REBUNI c2;

    REBCNT n;
    for (n = 0; n < len1; n++) { // includes terminator
        c1 = GET_CHAR_AT(s1, n);
        c2 = GET_CHAR_AT(s2, n);

        if (LO_CASE(c1) != LO_CASE(c2))
            return false; // all chars must match
    }

    // a/b matches: a/b, a/b/, a/b/c
    //
    c2 = GET_CHAR_AT(s2, n);
    return did (
            n >= len1 // all chars matched
            and  // Must be at end or at dir sep:
            (c1 == '/' or c1 == '\\'
            or c2 == 0 or c2 == '/' or c2 == '\\')
    );
}


//
//  Compare_Uni_Str: C
//
// Compare two ranges of string data.  Return lexical difference.
//
// Uncase: compare is case-insensitive.
//
REBINT Compare_Uni_Str(
    const REBYTE* bp1,
    const REBYTE* bp2,
    REBCNT len,
    bool uncase
){
    REBCHR(const*) u1 = cast(REBCHR(const*), bp1);
    REBCHR(const*) u2 = cast(REBCHR(const*), bp2);

    for (; len > 0; len--) {
        REBUNI c1;
        REBUNI c2;

        u1 = NEXT_CHR(&c1, u1);
        u2 = NEXT_CHR(&c2, u2);

        REBINT d;
        if (uncase)
            d = LO_CASE(c1) - LO_CASE(c2);
        else
            d = c1 - c2;

        if (d != 0)
            return d;
    }

    return 0;
}


//
//  Compare_String_Vals: C
//
// Compare two string values. Either can be byte or unicode wide.
//
// Uncase: compare is case-insensitive.
//
// Used for: general string comparions (various places)
//
REBINT Compare_String_Vals(const REBCEL *v1, const REBCEL *v2, bool uncase)
{
    assert(CELL_KIND(v1) != REB_BINARY and CELL_KIND(v2) != REB_BINARY);

    REBCNT l1  = VAL_LEN_AT(v1);
    REBCNT l2  = VAL_LEN_AT(v2);
    REBCNT len = MIN(l1, l2);

    REBINT n = Compare_Uni_Str(
        VAL_STRING_AT(v1),  // as a REBYTE* (can't put REBCHR(*) in %sys-core.h)
        VAL_STRING_AT(v2),
        len,
        uncase
    );

    if (n != 0)
        return n;

    return l1 - l2;
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
    REBSIZ l1 = LEN_BYTES(s1);
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
REBCNT Find_Bin_In_Bin(
    REBSER *series,
    REBCNT offset,
    const REBYTE *bp2,
    REBSIZ size2,
    REBFLGS flags // AM_FIND_MATCH
){
    assert(SER_LEN(series) >= offset);
    assert((flags & ~AM_FIND_MATCH) == 0);  // no AM_FIND_CASE

    if (size2 == 0 || (size2 + offset) > BIN_LEN(series))
        return NOT_FOUND; // pattern empty or is longer than the target

    REBYTE *bp1 = BIN_AT(series, offset);
    REBCNT size1 = BIN_LEN(series) - offset;

    REBYTE *end1 = bp1 + ((flags & AM_FIND_MATCH) ? 1 : size1 - (size2 - 1));

    REBYTE b2 = *bp2; // first byte

    while (bp1 != end1) {
        if (*bp1 == b2) { // matched first byte
            REBCNT n;
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
REBCNT Find_Str_In_Bin(
    REBSER *series, // binary series to search in
    REBCNT offset, // where to begin search at
    const REBYTE *bp2, // pointer to UTF-8 data to search (guaranteed valid)
    REBCNT len2, // codepoint count of the UTF-8 data of interest
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

    if (size2 == 0 or (size2 + offset) > SER_LEN(series))
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

            REBCNT n;
            for (n = 1; n < len2; n++) {
                if (*temp1 < 0x80)
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
REBCNT Find_Str_In_Str(
    REBSTR *str1,
    REBCNT index_unsigned,
    REBCNT end_unsigned,
    REBINT skip,
    REBSTR *str2,
    REBINT index2,
    REBCNT len,
    REBFLGS flags
){
    // Signed quantities used to allow stepping outside of bounds and still
    // being able to compare, but incoming parameters should not be negative.
    //
    REBINT index = index_unsigned;
    REBINT end = end_unsigned;

    REBINT start;
    if (skip < 0)
        start = 0;
    else
        start = index;

    assert((flags & ~(AM_FIND_CASE | AM_FIND_MATCH)) == 0);

    bool uncase = not (flags & AM_FIND_CASE); // case insenstive

    REBUNI c2_canon; // calculate first char lowercase once, vs. each step
    REBCHR(const*) next2 = STR_AT(str2, index2);
    next2 = NEXT_CHR(&c2_canon, next2);
    if (uncase)
        c2_canon = LO_CASE(c2_canon);

    REBCHR(const*) cp1 = STR_AT(str1, index);
    REBUNI c1;
    if (skip > 0)
        cp1 = NEXT_CHR(&c1, cp1);
    else
        cp1 = BACK_CHR(&c1, cp1);

    while (skip < 0 ? index >= start : index < end) {
        if (c1 == c2_canon or (uncase and LO_CASE(c1) == c2_canon)) {
            REBCHR(const*) tp1;
            if (skip > 0)
                tp1 = cp1;
            else
                tp1 = NEXT_STR(NEXT_STR(cp1)); // compensate from overstep

            REBCHR(const*) tp2 = next2;
            REBCNT n;
            for (n = 1; n < len; n++) {
                tp1 = NEXT_CHR(&c1, tp1);

                REBUNI c2;
                tp2 = NEXT_CHR(&c2, tp2);
                if (c1 == c2 or (uncase and LO_CASE(c1) == LO_CASE(c2)))
                    continue;

                break;
            }
            if (n == len)
                return index;
        }
        if (flags & AM_FIND_MATCH)
            break;

        cp1 = SKIP_CHR(&c1, cp1, skip);
        index += skip;
    }

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
REBCNT Find_Char_In_Str(
    REBUNI uni,         // character to look for
    REBSTR *s,          // UTF-8 string series
    REBCNT index_orig,  // first index to examine (if out of range, NOT_FOUND)
    REBCNT highest,     // *one past* highest return result (e.g. SER_LEN)
    REBINT skip,        // step amount while searching, can be negative!
    REBFLGS flags       // AM_FIND_CASE, AM_FIND_MATCH
){
    assert((flags & ~(AM_FIND_CASE | AM_FIND_MATCH)) == 0);

    // !!! In UTF-8, finding a char in a string is really just like finding a
    // string in a string.  Optimize as this all folds together.

    REBSTR *temp = Make_Codepoint_String(uni);

    REBCNT i = Find_Str_In_Str(
        s,
        index_orig,
        highest,
        skip,
        temp,
        0,
        1,
        flags
    );
    Free_Unmanaged_Series(SER(temp));

    return i;
}


//
//  Find_Char_In_Bin: C
//
REBCNT Find_Char_In_Bin(
    REBUNI uni,         // character to look for
    REBBIN *bin,        // binary series
    REBCNT lowest,      // lowest return index
    REBCNT index_orig,  // first index to examine (if out of range, NOT_FOUND)
    REBCNT highest,     // *one past* highest return result (e.g. SER_LEN)
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

    REBSTR *temp = Make_Codepoint_String(uni);

    REBCNT i = Find_Str_In_Bin(
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
REBCNT Find_Bin_Bitset(
    REBSER *bin,
    REBINT head,
    REBINT offset,
    REBINT tail,
    REBINT skip,
    REBSER *bset,
    REBFLGS flags
){
    assert(head >= 0 && tail >= 0 && offset >= 0);

    assert((flags & ~AM_FIND_MATCH) == 0); // no AM_FIND_CASE

    REBYTE *bp1 = BIN_AT(bin, offset);

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
REBCNT Find_Str_Bitset(
    REBSTR *str,
    REBCNT index_unsigned,
    REBCNT end_unsigned,
    REBINT skip,
    REBSER *bset,
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
        cp1 = NEXT_CHR(&c1, cp1);
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
REBCNT Count_Lines(REBYTE *bp, REBCNT len)
{
    REBCNT count = 0;

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
REBCNT Next_Line(REBYTE **bin)
{
    REBCNT count = 0;
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
//  Find_In_Any_Sequence: C
//
// !!! In R3-Alpha, the code for PARSE shared some of the same subroutines in
// %s-find.c as the FIND action.  However, there was still a lot of parallel
// logic in their invocation.  This is an attempt to further factor the common
// code, which hopefully will mean more consistency (as well as less code).
//
REBCNT Find_In_Any_Sequence(
    REBCNT *len,  // length of match (e.g. if pattern is a TAG!, includes <>)
    const RELVAL *any_series,
    const RELVAL *pattern,
    REBFLGS flags
){
    REBCNT index = VAL_INDEX(any_series);
    REBCNT end = VAL_LEN_HEAD(any_series);
    REBINT skip = 1;

    if (IS_BINARY(any_series))
        return find_binary(  // Note: returned len is in bytes here
            len, VAL_SERIES(any_series), index, end, pattern, flags, skip
        );

    if (ANY_STRING(any_series))
        return find_string(  // Note: returned len is in codepoints here
            len, VAL_STRING(any_series), index, end, pattern, flags, skip
        );

    fail ("Unknown sequence type for Find_In_Any_Sequence()");
}
