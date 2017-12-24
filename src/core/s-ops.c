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
// Returns TRUE if byte string does not use upper code page
// (e.g. no 128-255 characters)
//
REBOOL All_Bytes_ASCII(REBYTE *bp, REBCNT len)
{
    for (; len > 0; len--, bp++)
        if (*bp >= 0x80) return FALSE;

    return TRUE;
}


//
//  Temp_Byte_Chars_May_Fail: C
//
// NOTE: This function returns a temporary result, and uses an internal
// buffer.  Do not use it recursively.  Also, it will Trap on errors.
//
// Prequalifies a string before using it with a function that
// expects it to be 8-bits.  It would be used for instance to convert
// a string that is potentially REBUNI-wide into a form that can be used
// with a Scan_XXX routine, that is expecting ASCII or UTF-8 source.
// (Many TO-XXX conversions from STRING re-use that scanner logic.)
//
// Returns a temporary string and sets the length field.
//
// If `allow_utf8`, the constructed result is converted to UTF8.
//
// Checks or converts it:
//
//     1. it is byte string (not unicode)
//     2. if unicode, copy and return as temp byte string
//     3. it's actual content (less space, newlines) <= max len
//     4. it does not contain other values ("123 456")
//     5. it's not empty or only whitespace
//
REBYTE *Temp_Byte_Chars_May_Fail(
    const REBVAL *val,
    REBINT max_len,
    REBCNT *length,
    REBOOL allow_utf8
) {
    REBCNT tail = VAL_LEN_HEAD(val);
    REBCNT index = VAL_INDEX(val);
    REBCNT len;
    REBUNI c;
    REBYTE *bp;
    REBSER *src = VAL_SERIES(val);

    if (index > tail) fail (Error_Past_End_Raw());

    Resize_Series(BYTE_BUF, max_len+1);
    bp = BIN_HEAD(BYTE_BUF);

    // Skip leading whitespace:
    for (; index < tail; index++) {
        c = GET_ANY_CHAR(src, index);
        if (!IS_SPACE(c)) break;
    }

    // Copy chars that are valid:
    for (; index < tail; index++) {
        c = GET_ANY_CHAR(src, index);
        if (c >= 0x80) {
            if (!allow_utf8) fail (Error_Invalid_Chars_Raw());

            len = Encode_UTF8_Char(bp, c);
            max_len -= len;
            bp += len;
        }
        else if (!IS_SPACE(c)) {
            *bp++ = (REBYTE)c;
            max_len--;
        }
        else break;
        if (max_len < 0)
            fail (Error_Too_Long_Raw());
    }

    // Rest better be just spaces:
    for (; index < tail; index++) {
        c = GET_ANY_CHAR(src, index);
        if (!IS_SPACE(c)) fail (Error_Invalid_Chars_Raw());
    }

    *bp = '\0';

    len = bp - BIN_HEAD(BYTE_BUF);
    if (len == 0) fail (Error_Too_Short_Raw());

    if (length) *length = len;

    return BIN_HEAD(BYTE_BUF);
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
REBSER *Temp_UTF8_At_Managed(const RELVAL *str, REBCNT *index, REBCNT *length)
{
#if !defined(NDEBUG)
    if (NOT(ANY_STRING(str))) {
        printf("Temp_UTF8_At_Managed() called on non-ANY-STRING!");
        panic (str);
    }
#endif

    REBCNT len = (length != NULL && *length) ? *length : VAL_LEN_AT(str);

    // We do not want the temporary string to use OPT_ENC_CRLF_MAYBE...because
    // this is used for things like COMPRESS, and the Windows version of
    // Rebol needs to generate compatible compression with the Linux version.
    //
    REBSER *s = Make_UTF8_From_Any_String(str, len, OPT_ENC_0);
    MANAGE_SERIES(s);
    SET_SER_INFO(s, SERIES_INFO_FROZEN);

    if (index != NULL)
        *index = 0;
    if (length != NULL)
        *length = SER_LEN(s);

    assert(BYTE_SIZE(s));
    return s;
}


//
//  Xandor_Binary: C
//
// Only valid for BINARY data.
//
REBSER *Xandor_Binary(REBCNT action, REBVAL *value, REBVAL *arg)
{
        REBSER *series;
        REBYTE *p0 = VAL_BIN_AT(value);
        REBYTE *p1 = VAL_BIN_AT(arg);
        REBYTE *p2;
        REBCNT i;
        REBCNT mt, t1, t0, t2;

        t0 = VAL_LEN_AT(value);
        t1 = VAL_LEN_AT(arg);

        mt = MIN(t0, t1); // smaller array size
        // For AND - result is size of shortest input:
//      if (action == A_AND || (action == 0 && t1 >= t0))
//          t2 = mt;
//      else
        t2 = MAX(t0, t1);

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
            SET_SERIES_LEN(series, t2);
        }

        p2 = BIN_HEAD(series);

        switch (action) {
        case SYM_AND_T: // and~
            for (i = 0; i < mt; i++) *p2++ = *p0++ & *p1++;
            CLEAR(p2, t2 - mt);
            return series;

        case SYM_OR_T: // or~
            for (i = 0; i < mt; i++) *p2++ = *p0++ | *p1++;
            break;

        case SYM_XOR_T: // xor~
            for (i = 0; i < mt; i++) *p2++ = *p0++ ^ *p1++;
            break;

        default:
            // special bit set case EXCLUDE:
            for (i = 0; i < mt; i++) *p2++ = *p0++ & ~*p1++;
            if (t0 > t1) memcpy(p2, p0, t0 - t1); // residual from first only
            return series;
        }

        // Copy the residual:
        memcpy(p2, ((t0 > t1) ? p0 : p1), t2 - mt);
        return series;
}


//
//  Complement_Binary: C
//
// Only valid for BINARY data.
//
REBSER *Complement_Binary(REBVAL *value)
{
        REBSER *series;
        REBYTE *str = VAL_BIN_AT(value);
        REBINT len = VAL_LEN_AT(value);
        REBYTE *out;

        series = Make_Binary(len);
        SET_SERIES_LEN(series, len);
        out = BIN_HEAD(series);
        for (; len > 0; len--) {
            *out++ = ~(*str);
            ++str;
        }

        return series;
}


//
//  Shuffle_String: C
//
// Randomize a string. Return a new string series.
// Handles both BYTE and UNICODE strings.
//
void Shuffle_String(REBVAL *value, REBOOL secure)
{
    REBCNT n;
    REBCNT k;
    REBSER *series = VAL_SERIES(value);
    REBCNT idx     = VAL_INDEX(value);
    REBUNI swap;

    for (n = VAL_LEN_AT(value); n > 1;) {
        k = idx + (REBCNT)Random_Int(secure) % n;
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
    assert(NOT_SER_FLAG(src, SERIES_FLAG_ARRAY));

    REBOOL unicode = NOT(BYTE_SIZE(src));
    REBCNT tail;
    REBUNI c;

    for (tail = SER_LEN(src); tail > 0; tail--) {
        c = unicode ? *UNI_AT(src, tail - 1) : *BIN_AT(src, tail - 1);
        if (c != chr) break;
    }
    SET_SERIES_LEN(src, tail);
    TERM_SEQUENCE(src);
}


//
//  Change_Case: C
//
// Common code for string case handling.
//
void Change_Case(REBVAL *out, REBVAL *val, REBVAL *part, REBOOL upper)
{
    REBCNT len;
    REBCNT n;

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

    len = Partial(val, 0, part);
    n = VAL_INDEX(val);
    len += n;

    if (VAL_BYTE_SIZE(val)) {
        REBYTE *bp = VAL_BIN_HEAD(val);
        if (upper)
            for (; n < len; n++) bp[n] = (REBYTE)UP_CASE(bp[n]);
        else {
            for (; n < len; n++) bp[n] = (REBYTE)LO_CASE(bp[n]);
        }
    } else {
        REBUNI *up = VAL_UNI_HEAD(val);
        if (upper) {
            for (; n < len; n++) {
                if (up[n] < UNICODE_CASES) up[n] = UP_CASE(up[n]);
            }
        }
        else {
            for (; n < len; n++) {
                if (up[n] < UNICODE_CASES) up[n] = LO_CASE(up[n]);
            }
        }
    }
}


//
//  Split_Lines: C
//
// Given a string series, split lines on CR-LF.  Give back array of strings.
//
REBARR *Split_Lines(const REBVAL *str)
{
    REBDSP dsp_orig = DSP;

    REBSER *s = VAL_SERIES(str);
    REBCNT len = VAL_LEN_AT(str);
    REBCNT i = VAL_INDEX(str);

    REBCHR(const *) start = VAL_UNI_AT(str);
    REBCHR(const *) up = start;

    if (i == len)
        return Make_Array(0);

    REBUNI c;
    up = NEXT_CHR(&c, up);
    ++i;

    while (i < len) {
        if (c == LF || c == CR) {
            DS_PUSH_TRASH;
            Init_String(
                DS_TOP,
                Copy_Sequence_At_Len(
                    s,
                    AS_REBUNI(start) - UNI_HEAD(s),
                    AS_REBUNI(up) - AS_REBUNI(start) - 1
                )
            );
            SET_VAL_FLAG(DS_TOP, VALUE_FLAG_LINE);
            ++i;
            start = up;
            if (c == CR) {
                up = NEXT_CHR(&c, up);
                ++i;
                if (c == LF)
                    start = up; // remark start, fall through and fetch again
                else
                    continue; // already did next character fetch
            }
        }

        ++i;
        up = NEXT_CHR(&c, up);
    }

    // Possible remainder (no terminator)

    if (AS_REBUNI(up) > AS_REBUNI(start)) {
        DS_PUSH_TRASH;
        Init_String(
            DS_TOP,
            Copy_Sequence_At_Len(
                s,
                AS_REBUNI(start) - UNI_HEAD(s),
                AS_REBUNI(up) - AS_REBUNI(start) // no -1, wasn't terminated
            )
        );
        SET_VAL_FLAG(DS_TOP, VALUE_FLAG_LINE);
    }

    return Pop_Stack_Values(dsp_orig);
}
