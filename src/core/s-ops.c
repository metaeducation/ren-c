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
//  All_Codepoints_Latin1: C
//
// Returns TRUE if uni string needs 16 bits.
//
REBOOL All_Codepoints_Latin1(const REBUNI *up, REBCNT len)
{
    for (; len > 0; len--, up++)
        if (*up >= 0x100) return TRUE;

    return FALSE;
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
//  Deline_Uni: C
//
// This function converts any combination of CR and LF line endings to the
// internal REBOL line ending.
//
// The new length of the buffer is returned.
//
REBCNT Deline_Uni(REBUNI *buf, REBCNT len)
{
    REBUNI *cp = buf;
    REBUNI *tp = buf;

    while (cp < buf + len) {
        REBUNI c;
        if ((c = *cp++) == LF) {
            if (*cp == CR) cp++;
        }
        else if (c == CR) {
            c = LF;
            if (*cp == LF) cp++;
        }
        *tp++ = c;
    }
    *tp = '\0';

    return cast(REBCNT, tp - buf);
}


//
//  Enline_Uni: C
//
void Enline_Uni(REBSER *ser, REBCNT idx, REBCNT len)
{
    REBCNT cnt = 0;
    REBUNI *bp;
    REBUNI c = 0;
    REBCNT tail;

    // Calculate the size difference by counting the number of LF's
    // that have no CR's in front of them.
    bp = UNI_AT(ser, idx);
    for (; len > 0; len--) {
        if (*bp == LF && c != CR) cnt++;
        c = *bp++;
    }
    if (cnt == 0) return;

    // Extend series:
    len = SER_LEN(ser); // before expansion
    EXPAND_SERIES_TAIL(ser, cnt);
    tail = SER_LEN(ser); // after expansion
    bp = UNI_HEAD(ser); // expand may change it

    // Add missing CRs:
    while (cnt > 0) {
        bp[tail--] = bp[len]; // Copy src to dst.
        if (bp[len] == LF && (len == 0 || bp[len - 1] != CR)) {
            bp[tail--] = CR;
            cnt--;
        }
        len--;
    }
}


//
//  Entab_Unicode: C
//
// Entab a string and return a new series.
//
REBSER *Entab_Unicode(
    const REBUNI *up,
    REBCNT index,
    REBCNT len,
    REBINT tabsize
){
    DECLARE_MOLD (mo);
    SET_MOLD_FLAG(mo, MOLD_FLAG_RESERVE);
    mo->reserve = len;

    Push_Mold(mo);

    REBUNI *dp = UNI_AT(mo->series, mo->start);

    REBINT n = 0;
    for (; index < len; index++) {
        REBUNI c = up[index];

        // Count leading spaces, insert TAB for each tabsize:
        if (c == ' ') {
            if (++n >= tabsize) {
                *dp++ = '\t';
                n = 0;
            }
            continue;
        }

        // Hitting a leading TAB resets space counter:
        if (c == '\t') {
            *dp++ = cast(REBYTE, c);
            n = 0;
        }
        else {
            // Incomplete tab space, pad with spaces:
            for (; n > 0; n--)
                *dp++ = ' ';

            // Copy chars thru end-of-line (or end of buffer):
            while (index < len) {
                if ((*dp++ = up[index++]) == '\n')
                    break;
            }
        }
    }

    TERM_UNI_LEN(
        mo->series,
        mo->start + cast(REBCNT, dp - UNI_AT(mo->series, mo->start))
    );

    return Pop_Molded_String(mo);
}


//
//  Detab_Unicode: C
//
// Detab a unicode string and return a new series.
//
REBSER *Detab_Unicode(
    const REBUNI *up,
    REBCNT index,
    REBCNT len,
    REBINT tabsize
){
    DECLARE_MOLD (mo);

    // Estimate new length based on tab expansion:
    REBCNT count = 0;
    REBCNT n;
    for (n = index; n < len; n++)
        if (up[n] == '\t') // tab character
            ++count;

    SET_MOLD_FLAG(mo, MOLD_FLAG_RESERVE);
    mo->reserve = len + (count * (tabsize - 1));

    Push_Mold(mo);

    REBUNI *dp = UNI_AT(mo->series, mo->start);

    n = 0;
    while (index < len) {
        REBUNI c = up[index++];

        if (c == '\t') {
            *dp++ = ' ';
            n++;
            for (; n % tabsize != 0; n++)
                *dp++ = ' ';
            continue;
        }

        if (c == '\n')
            n = 0;
        else
            ++n;

        *dp++ = c;
    }

    TERM_UNI_LEN(
        mo->series,
        mo->start + cast(REBCNT, dp - UNI_AT(mo->series, mo->start))
    );

    return Pop_Molded_String(mo);
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
        REBYTE *bp = VAL_BIN(val);
        if (upper)
            for (; n < len; n++) bp[n] = (REBYTE)UP_CASE(bp[n]);
        else {
            for (; n < len; n++) bp[n] = (REBYTE)LO_CASE(bp[n]);
        }
    } else {
        REBUNI *up = VAL_UNI(val);
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
// Given a string series, split lines on CR-LF.
// Series can be bytes or Unicode.
//
REBARR *Split_Lines(REBVAL *str)
{
    REBDSP dsp_orig = DSP;

    REBSER *s = VAL_SERIES(str);
    REBCNT len = VAL_LEN_AT(str);
    REBCNT i = VAL_INDEX(str);

    REBCNT start = i;

    while (i < len) {
        REBUNI c = GET_ANY_CHAR(s, i);
        if (c == LF || c == CR) {
            DS_PUSH_TRASH;
            Init_String(
                DS_TOP,
                Copy_String_At_Len(s, start, i - start)
            );
            SET_VAL_FLAG(DS_TOP, VALUE_FLAG_LINE);
            ++i;
            if (c == CR && GET_ANY_CHAR(s, i) == LF)
                ++i;
            start = i;
        }
        else
            ++i;
    }
    // Possible remainder (no terminator)
    if (i > start) {
        DS_PUSH_TRASH;
        Init_String(
            DS_TOP,
            Copy_String_At_Len(s, start, i - start)
        );
        SET_VAL_FLAG(DS_TOP, VALUE_FLAG_LINE);
    }

    return Pop_Stack_Values(dsp_orig);
}
