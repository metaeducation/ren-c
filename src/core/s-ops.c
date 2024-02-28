//
//  File: %s-ops.c
//  Summary: "string handling utilities"
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
//  All_Bytes_ASCII: C
//
// Returns true if byte string does not use upper code page
// (e.g. no 128-255 characters)
//
bool All_Bytes_ASCII(REBYTE *bp, REBLEN len)
{
    for (; len > 0; len--, bp++)
        if (*bp >= 0x80)
            return false;

    return true;
}


//
//  Analyze_String_For_Scan: C
//
// Locate beginning byte pointer and number of bytes to prepare a string
// into a form that can be used with a Scan_XXX routine.  Used for instance
// to MAKE DATE! from a STRING!.  Rules are:
//
//     1. it's actual content (less space, newlines) <= max len
//     2. it does not contain other values ("123 456")
//     3. it's not empty or only whitespace
//
// !!! Strings are in transition to becoming "UTF-8 Everywhere" but are not
// there yet.  So this routine can't actually give back a pointer compatible
// with the scan.  Leverages Temp_UTF8_At_Managed, so the pointer that is
// returned could be GC'd if it's not guarded and evaluator logic runs.
//
REBYTE *Analyze_String_For_Scan(
    REBSIZ *opt_size_out,
    const Value* any_string,
    REBLEN max_len // maximum length in *codepoints*
){
    Ucs2(const*) up = Cell_String_At(any_string);
    REBLEN index = VAL_INDEX(any_string);
    REBLEN len = VAL_LEN_AT(any_string);
    if (len == 0)
        fail (Error_Past_End_Raw());

    REBUNI c;

    // Skip leading whitespace
    //
    for (; index < len; ++index, --len) {
        up = Ucs2_Next(&c, up);
        if (not IS_SPACE(c))
            break;
    }

    // Skip up to max_len non-space characters.
    //
    REBLEN num_chars = 0;
    for (; len > 0;) {
        ++num_chars;
        --len;

        // The R3-Alpha code would fail with Error_Invalid_Chars_Raw() if
        // there were UTF-8 characters in most calls.  Only ANY-WORD! from
        // ANY-STRING! allowed it.  Though it's not clear why it wouldn't be
        // better to delegate to the scanning routine itself to give a
        // more pointed error... allow c >= 0x80 for now.

        if (num_chars > max_len)
            fail (Error_Too_Long_Raw());

        up = Ucs2_Next(&c, up);
        if (IS_SPACE(c)) {
            --len;
            break;
        }
    }

    // Rest better be just spaces
    //
    for (; len > 0; --len) {
        up = Ucs2_Next(&c, up);
        if (!IS_SPACE(c))
            fail (Error_Invalid_Chars_Raw());
    }

    if (num_chars == 0)
        fail (Error_Past_End_Raw());

    DECLARE_VALUE (reindexed);
    Move_Value(reindexed, any_string);
    VAL_INDEX(reindexed) = index;

    REBSIZ offset;
    REBSER *temp = Temp_UTF8_At_Managed(
        &offset, opt_size_out, reindexed, VAL_LEN_AT(reindexed)
    );

    return BIN_AT(temp, offset);
}


//
//  Temp_UTF8_At_Managed: C
//
// !!! This is a routine that detected whether an R3-Alpha string was ASCII
// and hence could be reused as-is for UTF-8 purposes.  If it could not, a
// temporary string would be created for the string (which would either be
// byte-sized and have codepoints > 128, or wide characters and thus be
// UTF-8 incompatible).
//
// After the UTF-8 Everywhere conversion, this routine will not be necessary
// because all strings will be usable as UTF-8.  But as an interim step for
// "Latin1 Nowhere" where all strings are wide, this will *always* involve
// an allocation.
//
// Mutation of the result is not allowed because those mutations will not
// be reflected in the original string, due to generation.  Once the routine
// is eliminated, use of the original string will mean getting whatever
// mutability characteristics the original had.
//
REBSER *Temp_UTF8_At_Managed(
    REBSIZ *offset_out,
    REBSIZ *opt_size_out,
    const Cell* str,
    REBLEN length_limit
){
#if !defined(NDEBUG)
    if (not ANY_STRING(str)) {
        printf("Temp_UTF8_At_Managed() called on non-ANY-STRING!");
        panic (str);
    }
#endif

    assert(length_limit <= VAL_LEN_AT(str));

    REBSER *s = Make_UTF8_From_Any_String(str, length_limit);
    assert(BYTE_SIZE(s));

    MANAGE_SERIES(s);
    SET_SER_INFO(s, SERIES_INFO_FROZEN);

    *offset_out = 0;
    if (opt_size_out != nullptr)
        *opt_size_out = SER_LEN(s);
    return s;
}


//
//  Xandor_Binary: C
//
// Only valid for BINARY data.
//
REBSER *Xandor_Binary(Value* verb, Value* value, Value* arg)
{
    REBYTE *p0 = VAL_BIN_AT(value);
    REBYTE *p1 = VAL_BIN_AT(arg);

    REBLEN t0 = VAL_LEN_AT(value);
    REBLEN t1 = VAL_LEN_AT(arg);

    REBLEN mt = MIN(t0, t1); // smaller array size

    // !!! This used to say "For AND - result is size of shortest input:" but
    // the code was commented out
    /*
        if (verb == A_AND || (verb == 0 && t1 >= t0))
            t2 = mt;
        else
            t2 = MAX(t0, t1);
    */

    REBLEN t2 = MAX(t0, t1);

    REBSER *series;
    if (IS_BITSET(value)) {
        //
        // Although bitsets and binaries share some implementation here,
        // they have distinct allocation functions...and bitsets need
        // to set the REBSER.misc.negated union field (BITS_NOT) as
        // it would be illegal to read it if it were cleared via another
        // element of the union.
        //
        assert(IS_BITSET(arg));
        series = Make_Bitset(t2 * 8);
    }
    else {
        // Ordinary binary
        //
        series = Make_Binary(t2);
        TERM_SEQUENCE_LEN(series, t2);
    }

    REBYTE *p2 = BIN_HEAD(series);

    switch (Cell_Word_Id(verb)) {
    case SYM_INTERSECT: { // and
        REBLEN i;
        for (i = 0; i < mt; i++)
            *p2++ = *p0++ & *p1++;
        CLEAR(p2, t2 - mt);
        return series; }

    case SYM_UNION: { // or
        REBLEN i;
        for (i = 0; i < mt; i++)
            *p2++ = *p0++ | *p1++;
        break; }

    case SYM_DIFFERENCE: { // xor
        REBLEN i;
        for (i = 0; i < mt; i++)
            *p2++ = *p0++ ^ *p1++;
        break; }

    case SYM_EXCLUDE: { // !!! not a "type action", word manually in %words.r
        REBLEN i;
        for (i = 0; i < mt; i++)
            *p2++ = *p0++ & ~*p1++;
        if (t0 > t1)
            memcpy(p2, p0, t0 - t1); // residual from first only
        return series; }

    default:
        fail (Error_Illegal_Action(REB_BINARY, verb));
    }

    // Copy the residual
    //
    memcpy(p2, ((t0 > t1) ? p0 : p1), t2 - mt);
    return series;
}


//
//  Complement_Binary: C
//
// Only valid for BINARY data.
//
REBSER *Complement_Binary(Value* value)
{
    const REBYTE *bp = VAL_BIN_AT(value);
    REBLEN len = VAL_LEN_AT(value);

    REBSER *bin = Make_Binary(len);
    TERM_SEQUENCE_LEN(bin, len);

    REBYTE *dp = BIN_HEAD(bin);
    for (; len > 0; len--, ++bp, ++dp)
        *dp = ~(*bp);

    return bin;
}


//
//  Shuffle_String: C
//
// Randomize a string. Return a new string series.
// Handles both BYTE and UNICODE strings.
//
void Shuffle_String(Value* value, bool secure)
{
    REBLEN n;
    REBLEN k;
    REBSER *series = VAL_SERIES(value);
    REBLEN idx     = VAL_INDEX(value);
    REBUNI swap;

    for (n = VAL_LEN_AT(value); n > 1;) {
        k = idx + (REBLEN)Random_Int(secure) % n;
        n--;
        swap = GET_ANY_CHAR(series, k);
        SET_ANY_CHAR(series, k, GET_ANY_CHAR(series, n + idx));
        SET_ANY_CHAR(series, n + idx, swap);
    }
}


//
//  Trim_Tail: C
//
// Used to trim off hanging spaces during FORM and MOLD.
//
void Trim_Tail(REBSER *src, REBYTE chr)
{
    assert(BYTE_SIZE(src)); // mold buffer

    REBLEN tail;
    for (tail = SER_LEN(src); tail > 0; tail--) {
        REBUNI c = *BIN_AT(src, tail - 1);
        if (c != chr)
            break;
    }
    SET_SERIES_LEN(src, tail);
    TERM_SEQUENCE(src);
}


//
//  Change_Case: C
//
// Common code for string case handling.
//
void Change_Case(Value* out, Value* val, Value* part, bool upper)
{
    Move_Value(out, val);

    if (IS_CHAR(val)) {
        REBUNI c = VAL_CHAR(val);
        if (c < UNICODE_CASES) {
            c = upper ? UP_CASE(c) : LO_CASE(c);
        }
        VAL_CHAR(out) = c;
        return;
    }

    // String series:

    FAIL_IF_READ_ONLY_SERIES(VAL_SERIES(val));

    REBLEN len = Part_Len_May_Modify_Index(val, part);
    REBLEN n = 0;

    if (VAL_BYTE_SIZE(val)) {
        REBYTE *bp = VAL_BIN_AT(val);
        if (upper)
            for (; n != len; n++)
                bp[n] = cast(REBYTE, UP_CASE(bp[n]));
        else {
            for (; n != len; n++)
                bp[n] = cast(REBYTE, LO_CASE(bp[n]));
        }
    } else {
        REBUNI *up = Cell_String_At(val);
        if (upper) {
            for (; n != len; n++) {
                if (up[n] < UNICODE_CASES)
                    up[n] = UP_CASE(up[n]);
            }
        }
        else {
            for (; n != len; n++) {
                if (up[n] < UNICODE_CASES)
                    up[n] = LO_CASE(up[n]);
            }
        }
    }
}


//
//  Split_Lines: C
//
// Given a string series, split lines on CR-LF.  Give back array of strings.
//
// Note: The definition of "line" in POSIX is a sequence of characters that
// end with a newline.  Hence, the last line of a file should have a newline
// marker, or it's not a "line")
//
// https://stackoverflow.com/a/729795
//
// This routine does not require it.
//
// !!! CR support is likely to be removed...and CR will be handled as a normal
// character, with special code needed to process it.
//
REBARR *Split_Lines(const Value* str)
{
    REBDSP dsp_orig = DSP;

    REBSER *s = VAL_SERIES(str);
    REBLEN len = VAL_LEN_AT(str);
    REBLEN i = VAL_INDEX(str);

    Ucs2(const*) start = Cell_String_At(str);
    Ucs2(const*) up = start;

    if (i == len)
        return Make_Arr(0);

    REBUNI c;
    up = Ucs2_Next(&c, up);
    ++i;

    while (i != len) {
        if (c == LF or c == CR) {
            DS_PUSH_TRASH;
            Init_Text(
                DS_TOP,
                Copy_Sequence_At_Len(
                    s,
                    AS_REBUNI(start) - String_Head(s),
                    AS_REBUNI(up) - AS_REBUNI(start) - 1
                )
            );
            SET_VAL_FLAG(DS_TOP, VALUE_FLAG_NEWLINE_BEFORE);
            start = up;
            if (c == CR) {
                up = Ucs2_Next(&c, up);
                ++i;
                if (i == len)
                    break; // if it was the last CR/LF don't fetch again

                if (c != LF)
                    continue; // already did next character fetch

                start = up; // remark start, fall through and fetch again
            }
        }

        ++i;
        up = Ucs2_Next(&c, up);
    }

    // `c` is now the last character in the string.  See remarks above about
    // not requiring the last character to be a newline.

    if (c == CR or c == LF)
        up = Ucs2_Back(nullptr, up); // back up

    if (AS_REBUNI(up) > AS_REBUNI(start)) {
        DS_PUSH_TRASH;
        Init_Text(
            DS_TOP,
            Copy_Sequence_At_Len(
                s,
                AS_REBUNI(start) - String_Head(s),
                AS_REBUNI(up) - AS_REBUNI(start) // no -1, backed up if '\n'
            )
        );
        SET_VAL_FLAG(DS_TOP, VALUE_FLAG_NEWLINE_BEFORE);
    }

    return Pop_Stack_Values_Core(dsp_orig, ARRAY_FLAG_TAIL_NEWLINE);
}
