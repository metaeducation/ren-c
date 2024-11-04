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
//  Compare_Binary_Vals: C
//
// Compare two binary values.
//
// Compares bytes, not chars. Return the difference.
//
// Used for: Binary comparision function
//
REBINT Compare_Binary_Vals(const Cell* v1, const Cell* v2)
{
    REBLEN l1 = Cell_Series_Len_At(v1);
    REBLEN l2 = Cell_Series_Len_At(v2);
    REBLEN len = MIN(l1, l2);
    REBINT n;

    n = memcmp(
        Flex_Data_At(Flex_Wide(Cell_Flex(v1)), Cell_Flex(v1), VAL_INDEX(v1)),
        Flex_Data_At(Flex_Wide(Cell_Flex(v2)), Cell_Flex(v2), VAL_INDEX(v2)),
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
REBINT Compare_Bytes(const Byte *b1, const Byte *b2, REBLEN len, bool uncase)
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
const Byte *Match_Bytes(const Byte *src, const Byte *pat)
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
//  Compare_Uni_Str: C
//
// Compare two ranges of string data.  Return lexical difference.
//
// Uncase: compare is case-insensitive.
//
REBINT Compare_Uni_Str(
    Ucs2(const*) u1,
    Ucs2(const*) u2,
    REBLEN len,
    bool uncase
){
    for (; len > 0; len--) {
        Ucs2Unit c1;
        Ucs2Unit c2;

        u1 = Ucs2_Next(&c1, u1);
        u2 = Ucs2_Next(&c2, u2);

        REBINT d;
        if (uncase && c1 < UNICODE_CASES && c2 < UNICODE_CASES)
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
REBINT Compare_String_Vals(const Cell* v1, const Cell* v2, bool uncase)
{
    assert(not Is_Binary(v1) and not Is_Binary(v2));

    REBLEN l1  = Cell_Series_Len_At(v1);
    REBLEN l2  = Cell_Series_Len_At(v2);
    REBLEN len = MIN(l1, l2);

    REBINT n = Compare_Uni_Str(Cell_String_At(v1), Cell_String_At(v2), len, uncase);

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
REBINT Compare_UTF8(const Byte *s1, const Byte *s2, Size l2)
{
    Ucs2Unit c1, c2;
    Size l1 = LEN_BYTES(s1);
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
            if (c1 >= UNICODE_CASES || c2 >= UNICODE_CASES ||
                LO_CASE(c1) != LO_CASE(c2)) {
                return (c1 > c2) ? -1 : -3;
            }
            if (!result) result = (c1 > c2) ? 3 : 1;
        }
    }
    if (l1 != l2) result = (l1 > l2) ? -1 : -3;

    return result;
}


//
//  Find_Byte_Str: C
//
// Find a byte string within a byte string. Optimized for speed.
//
// Returns starting position or NOT_FOUND.
//
// Uncase: compare is case-insensitive.
// Match: compare to first position only.
//
// NOTE: Series tail must be > index.
//
REBLEN Find_Byte_Str(
    Binary* series,
    REBLEN index,
    Byte *b2,
    REBLEN l2,
    bool uncase,
    bool match
){
    Byte *b1;
    Byte *e1;
    REBLEN l1;
    Byte c;
    REBLEN n;

    // The pattern empty or is longer than the target:
    if (l2 == 0 || (l2 + index) > Binary_Len(series)) return NOT_FOUND;

    b1 = Binary_At(series, index);
    l1 = Binary_Len(series) - index;

    e1 = b1 + (match ? 1 : l1 - (l2 - 1));

    c = *b2; // first char

    if (!uncase) {

        while (b1 != e1) {
            if (*b1 == c) { // matched first char
                for (n = 1; n < l2; n++) {
                    if (b1[n] != b2[n]) break;
                }
                if (n == l2) return (b1 - Binary_Head(series));
            }
            b1++;
        }

    } else {

        c = (Byte)LO_CASE(c); // OK! (never > 255)

        while (b1 != e1) {
            if (LO_CASE(*b1) == c) { // matched first char
                for (n = 1; n != l2; n++) {
                    if (LO_CASE(b1[n]) != LO_CASE(b2[n])) break;
                }
                if (n == l2) return (b1 - Binary_Head(series));
            }
            b1++;
        }

    }

    return NOT_FOUND;
}


//
//  Find_Str_Str: C
//
// General purpose find a substring.
//
// Supports: forward/reverse with skip, cased/uncase, Unicode/byte.
//
// Skip can be set positive or negative (for reverse).
//
// Flags are set according to ALL_FIND_REFS
//
REBLEN Find_Str_Str(Flex* ser1, REBLEN head, REBLEN index, REBLEN tail, REBINT skip, Flex* ser2, REBLEN index2, REBLEN len, REBLEN flags)
{
    Ucs2Unit c1;
    Ucs2Unit c2;
    Ucs2Unit c3;
    REBLEN n = 0;
    bool uncase = not (flags & AM_FIND_CASE); // case insenstive

    c2 = GET_ANY_CHAR(ser2, index2); // starting char
    if (uncase && c2 < UNICODE_CASES) c2 = LO_CASE(c2);

    for (; index >= head && index < tail; index += skip) {

        c1 = GET_ANY_CHAR(ser1, index);
        if (uncase && c1 < UNICODE_CASES) c1 = LO_CASE(c1);

        if (c1 == c2) {
            for (n = 1; n < len; n++) {
                c1 = GET_ANY_CHAR(ser1, index+n);
                c3 = GET_ANY_CHAR(ser2, index2+n);
                if (uncase && c1 < UNICODE_CASES && c3 < UNICODE_CASES) {
                    if (LO_CASE(c1) != LO_CASE(c3)) break;
                } else {
                    if (c1 != c3) break;
                }
            }
            if (n == len) {
                if (flags & AM_FIND_TAIL) return index + len;
                return index;
            }
        }
        if (flags & AM_FIND_MATCH) break;
    }

    return NOT_FOUND;
}


#if RUNTIME_CHECKS

//
//  Find_Str_Char_Old: C
//
// The Find_Str_Char routine turned out to be kind of a bottleneck in code
// that was heavily reliant on PARSE, so it became slightly interesting to
// try and optimize it a bit.  The old routine is kept around for the
// moment (and maybe indefinitely) as a debug check to make sure the
// optimized routine gives back the same answer.
//
// Note: the old routine did not handle negative skips correctly, because
// index is unsigned and it tries to use a comparison crossing zero.  This
// is handled by the new version, and will be vetted separately.
//
static REBLEN Find_Str_Char_Old(
    Flex* flex,
    REBLEN head,
    REBLEN index,
    REBLEN tail,
    REBINT skip,
    Ucs2Unit c2,
    REBLEN flags
) {
    bool uncase = not (flags & AM_FIND_CASE); // case insensitive

    if (uncase && c2 < UNICODE_CASES) c2 = LO_CASE(c2);

    for (; index >= head && index < tail; index += skip) {
        Ucs2Unit c1 = GET_ANY_CHAR(flex, index);
        if (uncase && c1 < UNICODE_CASES)
            c1 = LO_CASE(c1);

        if (c1 == c2)
            return index;

        if (flags & AM_FIND_MATCH)
            break;
    }

    return NOT_FOUND;
}

#endif


//
//  Find_Str_Char: C
//
// General purpose find a char in a string, which works with both unicode and
// byte-sized strings.  Supports AM_FIND_CASE for case-sensitivity (as
// opposed to the case-insensitive default) and AM_FIND_MATCH to check only
// the character at the current position and then stop.
//
// Skip can be set positive or negative (for reverse), and will be bounded
// by the `start` and `end`.
//
// Note that features like "/LAST" are handled at a higher level and
// translated into SKIP=(-1) and starting at (highest - 1).
//
// *This routine is called a lot*, especially in PARSE.  So the seeming
// micro-optimization of it was motivated by that.  It's not all that
// complicated, in truth.  For the near-term, the old implementation of the
// routine is run in parallel as a debug check to ensure the same result
// is coming from the optimized code.
//
REBLEN Find_Str_Char(
    Ucs2Unit uni,         // character to look for
    Flex* series,     // series with width sizeof(Byte) or sizeof(Ucs2Unit)
    REBLEN lowest,      // lowest return index
    REBLEN index_orig,  // first index to examine (if out of range, NOT_FOUND)
    REBLEN highest,     // *one past* highest return result (e.g. SER_LEN)
    REBINT skip,        // step amount while searching, can be negative!
    Flags flags       // AM_FIND_CASE, AM_FIND_MATCH
) {
    // Because the skip may be negative, and we don't check before we step
    // and may "cross zero", it's necessary to use a signed index to be
    // able to notice that crossing.
    //
    REBINT index;

    // We establish an array of two potential cases we are looking for.
    // If there aren't actually two, this array sets both to be the same (vs.
    // using something like a '\0' in one cell if they are) because FIND is
    // able to seek NUL in strings.
    //
    Ucs2Unit casings[2];

    if (flags & AM_FIND_CASE) { // case-*sensitive*
        casings[0] = uni;
        casings[1] = uni;
    }
    else {
        casings[0] = uni < UNICODE_CASES ? LO_CASE(uni) : uni;
        casings[1] = uni < UNICODE_CASES ? UP_CASE(uni) : uni;
    }

    assert(lowest <= Flex_Len(series));
    assert(index_orig <= Flex_Len(series));
    assert(highest <= Flex_Len(series));

    // !!! Would skip = 0 be a clearer expression of /MATCH, as in "there
    // is no skip count"?  Perhaps in the interface as /SKIP NONE and then
    // translated to 0 for this internal call?
    //
    assert(skip != 0);

    // Rest of routine assumes we are inside of the range to begin with.
    //
    if (index_orig < lowest || index_orig >= highest || lowest == highest)
        goto return_not_found;

    // Past this point we'll be using the signed index.
    //
    index = cast(REBINT, index_orig);

    // /MATCH only does one check at the current position for the character
    // and then returns.  It basically subverts any optimization we might
    // try that uses memory range functions/etc, and if "/skip 0" were the
    // replacement for match it would have to be handled separately anyway.
    //
    if (flags & AM_FIND_MATCH) {
        Ucs2Unit single = GET_ANY_CHAR(series, index_orig);
        if (single == casings[0] || single == casings[1])
            goto return_index;
        goto return_not_found;
    }

    // If searching a potentially much longer string, take opportunities to
    // use optimized C library functions if possible.
    //
    if (BYTE_SIZE(series)) {
        Byte *bp = Binary_Head(cast(Binary*, series));
        Byte breakset[3];

        // We need to cover when the lowercase or uppercase variant of a
        // unicode character is <= 0xFF even though the character itself
        // is not.  Build our breakset while we're doing the test.  Note
        // that this handles the case-sensitive version fine because it
        // will be noticed if breakset[0] and breakset[1] are the same.
        //
        if (casings[0] > 0xFF) {
            if (casings[1] > 0xFF) goto return_not_found;

            breakset[0] = cast(Byte, casings[1]);
            breakset[1] = '\0';
        }
        else {
            breakset[0] = cast(Byte, casings[0]);

            if (casings[1] > 0xFF || casings[1] == casings[0]) {
                breakset[1] = '\0';
            }
            else {
                breakset[1] = cast(Byte, casings[1]);
                breakset[2] = '\0';
            }
        }

        // breakset[0] will be '\0' if we're literally searching for a '\0'.
        // But it will also be '\0' if no candidate we were searching for
        // would be byte-sized, and hence won't be found...so return NOT_FOUND
        // if the latter is true.
        //
        if (breakset[0] == '\0' && uni != '\0')
            goto return_not_found;

        if (skip == 1 && breakset[1] == '\0') {
            //
            // For case-sensitive comparisons, or if the character has no
            // distinction in upper and lower cases, or if only one of the
            // two unicode casings is byte-sized...we can use use the
            // optimized `memchr()` operation to find the single byte.
            // This can only work if SKIP is 1.
            //
            void *v = memchr(bp + index, breakset[0], highest - index);
            if (v) {
                index = cast(Byte*, v) - bp;
                goto return_index;
            }
        }
        else {
            // If the comparison is case-insensitive and the character has
            // a distinct upper and lower case, there are two candidate
            // characters we are looking for.
            //
            // We use a threshold to decide if it's worth it to use a library
            // routine that can only search forward to null terminators vs.
            // a for loop we can limit, run reverse, or skip by more than 1.
            // (<string.h> routines also can't be used to hunt for a 0 byte.)
            //
            if (
                skip == 1
                && (Flex_Len(series) - highest) < ((highest - lowest) / 2)
                && uni != '\0'
            ) {
                // The `strcspn()` optimized routine can be used to check for
                // a set of characters, and returns the number of characters
                // read before a match was found.  It will be the length of
                // the string if no match.
                //
                while (true) {
                    index += strcspn(
                        cast(char*, bp + index), cast(char*, breakset)
                    );
                    if (index >= cast(REBINT, highest))
                        goto return_not_found;

                    goto return_index;
                }
            }
            else {
                // We're skipping by more than one, going in reverse, or
                // looking for a '\0' byte.  Can't use any fancy tricks
                // (besides the trick of precalculating the casings)
                //
                while (true) {
                    if (bp[index] == breakset[0] || bp[index] == breakset[1])
                        goto return_index;

                    index += skip;
                    if (index < cast(REBINT, lowest)) break;
                    if (index >= cast(REBINT, highest)) break;
                }
            }
        }
    }
    else {
        Ucs2Unit* up = String_Head(cast(String*, series));
        while (true) {
            if (up[index] == casings[0] || up[index] == casings[1])
                goto return_index;

            index += skip;
            if (index < cast(REBINT, lowest)) break;
            if (index >= cast(REBINT, highest)) break;
        }
    }

return_not_found:

#if RUNTIME_CHECKS
    assert(NOT_FOUND == Find_Str_Char_Old(
        series, lowest, index_orig, highest, skip, uni, flags
    ));
#endif
    return NOT_FOUND;

return_index:

#if RUNTIME_CHECKS
    assert(cast(REBLEN, index) == Find_Str_Char_Old(
        series, lowest, index_orig, highest, skip, uni, flags
    ));
#endif

    assert(index >= 0);
    return cast(REBLEN, index);
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
    Flex* flex,
    REBLEN head,
    REBLEN index,
    REBLEN tail,
    REBINT skip,
    Binary* bset,
    REBLEN flags
) {
    bool uncase = not (flags & AM_FIND_CASE); // case insensitive

    for (; index >= head && index < tail; index += skip) {
        Ucs2Unit c1 = GET_ANY_CHAR(flex, index);

        if (Check_Bit(bset, c1, uncase))
            return index;

        if (flags & AM_FIND_MATCH)
            break;
    }

    return NOT_FOUND;
}


//
//  Count_Lines: C
//
// Count lines in a UTF-8 file.
//
REBLEN Count_Lines(Byte *bp, REBLEN len)
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
REBLEN Next_Line(Byte **bin)
{
    REBLEN count = 0;
    Byte *bp = *bin;

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
