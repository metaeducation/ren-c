/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2012 REBOL Technologies
**  REBOL is a trademark of REBOL Technologies
**
**  Licensed under the Apache License, Version 2.0 (the "License");
**  you may not use this file except in compliance with the License.
**  You may obtain a copy of the License at
**
**  http://www.apache.org/licenses/LICENSE-2.0
**
**  Unless required by applicable law or agreed to in writing, software
**  distributed under the License is distributed on an "AS IS" BASIS,
**  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**  See the License for the specific language governing permissions and
**  limitations under the License.
**
************************************************************************
**
**  Module:  s-ops.c
**  Summary: string handling utilities
**  Section: strings
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

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
//  Is_Wide: C
// 
// Returns TRUE if uni string needs 16 bits.
//
REBOOL Is_Wide(const REBUNI *up, REBCNT len)
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
// Opts can be:
//     0 - no special options
//     1 - allow UTF8 (val is converted to UTF8 during qualification)
//     2 - allow binary
// 
// Checks or converts it:
// 
//     1. it is byte string (not unicode)
//     2. if unicode, copy and return as temp byte string
//     3. it's actual content (less space, newlines) <= max len
//     4. it does not contain other values ("123 456")
//     5. it's not empty or only whitespace
//
REBYTE *Temp_Byte_Chars_May_Fail(const REBVAL *val, REBINT max_len, REBCNT *length, REBINT opts)
{
    REBCNT tail = VAL_TAIL(val);
    REBCNT index = VAL_INDEX(val);
    REBCNT len;
    REBUNI c;
    REBYTE *bp;
    REBSER *src = VAL_SERIES(val);

    if (index > tail) fail (Error(RE_PAST_END));

    Resize_Series(BUF_FORM, max_len+1);
    bp = BIN_HEAD(BUF_FORM);

    // Skip leading whitespace:
    for (; index < tail; index++) {
        c = GET_ANY_CHAR(src, index);
        if (!IS_SPACE(c)) break;
    }

    // Copy chars that are valid:
    for (; index < tail; index++) {
        c = GET_ANY_CHAR(src, index);
        if (opts < 2 && c >= 0x80) {
            if (opts == 0) fail (Error(RE_INVALID_CHARS));
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
            fail (Error(RE_TOO_LONG));
    }

    // Rest better be just spaces:
    for (; index < tail; index++) {
        c = GET_ANY_CHAR(src, index);
        if (!IS_SPACE(c)) fail (Error(RE_INVALID_CHARS));
    }

    *bp= 0;

    len = bp - BIN_HEAD(BUF_FORM);
    if (len == 0) fail (Error(RE_TOO_SHORT));

    if (length) *length = len;

    return BIN_HEAD(BUF_FORM);
}


//
//  Temp_Bin_Str_Managed: C
// 
// Determines if UTF8 conversion is needed for a series before it
// is used with a byte-oriented function.
// 
// If conversion is needed, a UTF8 series will be created.  Otherwise,
// the source series is returned as-is.
// 
// Note: This routine should only be used to generate a value used
// for temporary purposes, because it has a "surprising variance"
// regarding its input.  If the value's series can be reused, it is--
// and this depends on an implementation detail of internal encoding
// that the user should not be aware of (they need not know if the
// internal representation of an ASCII string uses 1, 2, or however
// many bytes).  But copying vs. non-copying means the resulting
// data might or might not have previous values available to step
// back into from the originating series!
// 
// !!! Should performance dictate it, the callsites could be
// adapted to know whether this produced a new series or not, and
// instead of managing a created result they could be responsible
// for freeing it if so.
//
REBSER *Temp_Bin_Str_Managed(REBVAL *val, REBCNT *index, REBCNT *length)
{
    REBCNT len = (length && *length) ? *length : VAL_LEN(val);
    REBSER *series;

    assert(IS_BINARY(val) || ANY_STR(val));

    if (len == 0 || IS_BINARY(val) || VAL_STR_IS_ASCII(val)) {
        // If it's zero length, BINARY!, or an ANY-STRING! whose bytes are
        // all values less than 128, we reuse the series.

        series = VAL_SERIES(val);
        ASSERT_SERIES_MANAGED(series);

        if (index) *index = VAL_INDEX(val);
        if (length) *length = len;
    }
    else {
        // UTF-8 conversion is required, and we manage the result.

        series = Make_UTF8_From_Any_String(val, len, OPT_ENC_CRLF_MAYBE);
        MANAGE_SERIES(series);

        if (index) *index = 0;
        if (length) *length = SERIES_TAIL(series);
    }

    return series;
}


//
//  Xandor_Binary: C
// 
// Only valid for BINARY data.
//
REBSER *Xandor_Binary(REBCNT action, REBVAL *value, REBVAL *arg)
{
        REBSER *series;
        REBYTE *p0 = VAL_BIN_DATA(value);
        REBYTE *p1 = VAL_BIN_DATA(arg);
        REBYTE *p2;
        REBCNT i;
        REBCNT mt, t1, t0, t2;

        t0 = VAL_LEN(value);
        t1 = VAL_LEN(arg);

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
            SERIES_TAIL(series) = t2;
        }

        p2 = BIN_HEAD(series);

        switch (action) {
        case A_AND_T:
            for (i = 0; i < mt; i++) *p2++ = *p0++ & *p1++;
            CLEAR(p2, t2 - mt);
            return series;

        case A_OR_T:
            for (i = 0; i < mt; i++) *p2++ = *p0++ | *p1++;
            break;

        case A_XOR_T:
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
        REBYTE *str = VAL_BIN_DATA(value);
        REBINT len = VAL_LEN(value);
        REBYTE *out;

        series = Make_Binary(len);
        SERIES_TAIL(series) = len;
        out = BIN_HEAD(series);
        for (; len > 0; len--)
            *out++ = ~ *str++;

        return series;
}


//
//  Shuffle_String: C
// 
// Randomize a string. Return a new string series.
// Handles both BYTE and UNICODE strings.
//
void Shuffle_String(REBVAL *value, REBFLG secure)
{
    REBCNT n;
    REBCNT k;
    REBSER *series = VAL_SERIES(value);
    REBCNT idx     = VAL_INDEX(value);
    REBUNI swap;

    for (n = VAL_LEN(value); n > 1;) {
        k = idx + (REBCNT)Random_Int(secure) % n;
        n--;
        swap = GET_ANY_CHAR(series, k);
        SET_ANY_CHAR(series, k, GET_ANY_CHAR(series, n + idx));
        SET_ANY_CHAR(series, n + idx, swap);
    }
}


/*
#define SEED_LEN 10
static REBYTE seed_str[SEED_LEN] = {
    249, 52, 217, 38, 207, 59, 216, 52, 222, 61 // xor "Sassenrath" #{AA55..}
};
//      kp = seed_str; // Any seed constant.
//      klen = SEED_LEN;
*/

//
//  Cloak: C
// 
// Simple data scrambler. Quality depends on the key length.
// Result is made in place (data string).
// 
// The key (kp) is passed as a REBVAL or REBYTE (when klen is !0).
//
REBOOL Cloak(REBOOL decode, REBYTE *cp, REBCNT dlen, REBYTE *kp, REBCNT klen, REBFLG as_is)
{
    REBCNT i, n;
    REBYTE src[20];
    REBYTE dst[20];

    if (dlen == 0) return TRUE;

    // Decode KEY as VALUE field (binary, string, or integer)
    if (klen == 0) {
        REBVAL *val = (REBVAL*)kp;
        REBSER *ser;

        switch (VAL_TYPE(val)) {
        case REB_BINARY:
            kp = VAL_BIN_DATA(val);
            klen = VAL_LEN(val);
            break;
        case REB_STRING:
            ser = Temp_Bin_Str_Managed(val, &i, &klen);
            kp = BIN_SKIP(ser, i);
            break;
        case REB_INTEGER:
            INT_TO_STR(VAL_INT64(val), dst);
            klen = LEN_BYTES(dst);
            as_is = FALSE;
            break;
        }

        if (klen == 0) return FALSE;
    }

    if (!as_is) {
        for (i = 0; i < 20; i++) src[i] = kp[i % klen];
        SHA1(src, 20, dst);
        klen = 20;
        kp = dst;
    }

    if (decode)
        for (i = dlen-1; i > 0; i--) cp[i] ^= cp[i-1] ^ kp[i % klen];

    // Change starting byte based all other bytes.
    n = 0xa5;
    for (i = 1; i < dlen; i++) n += cp[i];
    cp[0] ^= (REBYTE)n;

    if (!decode)
        for (i = 1; i < dlen; i++) cp[i] ^= cp[i-1] ^ kp[i % klen];

    return TRUE;
}


//
//  Trim_Tail: C
// 
// Used to trim off hanging spaces during FORM and MOLD.
//
void Trim_Tail(REBSER *src, REBYTE chr)
{
    REBOOL is_uni = !BYTE_SIZE(src);
    REBCNT tail;
    REBUNI c;

    assert(!Is_Array_Series(src));

    for (tail = SERIES_TAIL(src); tail > 0; tail--) {
        c = is_uni ? *UNI_SKIP(src, tail - 1) : *BIN_SKIP(src, tail - 1);
        if (c != chr) break;
    }
    SERIES_TAIL(src) = tail;
    TERM_SEQUENCE(src);
}


//
//  Deline_Bytes: C
// 
// This function converts any combination of CR and
// LF line endings to the internal REBOL line ending.
// The new length of the buffer is returned.
//
REBCNT Deline_Bytes(REBYTE *buf, REBCNT len)
{
    REBYTE  c, *cp, *tp;

    cp = tp = buf;
    while (cp < buf + len) {
        if ((c = *cp++) == LF) {
            if (*cp == CR) cp++;
        }
        else if (c == CR) {
            c = LF;
            if (*cp == LF) cp++;
        }
        *tp++ = c;
    }
    *tp = 0;

    return (REBCNT)(tp - buf);
}


//
//  Deline_Uni: C
//
REBCNT Deline_Uni(REBUNI *buf, REBCNT len)
{
    REBUNI c, *cp, *tp;

    cp = tp = buf;
    while (cp < buf + len) {
        if ((c = *cp++) == LF) {
            if (*cp == CR) cp++;
        }
        else if (c == CR) {
            c = LF;
            if (*cp == LF) cp++;
        }
        *tp++ = c;
    }
    *tp = 0;

    return (REBCNT)(tp - buf);
}


//
//  Enline_Bytes: C
//
void Enline_Bytes(REBSER *ser, REBCNT idx, REBCNT len)
{
    REBCNT cnt = 0;
    REBYTE *bp;
    REBYTE c = 0;
    REBCNT tail;

    // Calculate the size difference by counting the number of LF's
    // that have no CR's in front of them.
    bp = BIN_SKIP(ser, idx);
    for (; len > 0; len--) {
        if (*bp == LF && c != CR) cnt++;
        c = *bp++;
    }
    if (cnt == 0) return;

    // Extend series:
    len = SERIES_TAIL(ser); // before expansion
    EXPAND_SERIES_TAIL(ser, cnt);
    tail = SERIES_TAIL(ser); // after expansion
    bp = BIN_HEAD(ser); // expand may change it

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
    bp = UNI_SKIP(ser, idx);
    for (; len > 0; len--) {
        if (*bp == LF && c != CR) cnt++;
        c = *bp++;
    }
    if (cnt == 0) return;

    // Extend series:
    len = SERIES_TAIL(ser); // before expansion
    EXPAND_SERIES_TAIL(ser, cnt);
    tail = SERIES_TAIL(ser); // after expansion
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
//  Entab_Bytes: C
// 
// Entab a string and return a new series.
//
REBSER *Entab_Bytes(REBYTE *bp, REBCNT index, REBCNT len, REBINT tabsize)
{
    REBINT n = 0;
    REBYTE *dp;
    REBYTE c;

    dp = Reset_Buffer(BUF_FORM, len);

    for (; index < len; index++) {

        c = bp[index];

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
            *dp++ = (REBYTE)c;
            n = 0;
        }
        else {
            // Incomplete tab space, pad with spaces:
            for (; n > 0; n--) *dp++ = ' ';

            // Copy chars thru end-of-line (or end of buffer):
            while (index < len) {
                if ((*dp++ = bp[index++]) == '\n') break;
            }
        }
    }

    return Copy_Buffer(BUF_FORM, dp);
}


//
//  Entab_Unicode: C
// 
// Entab a string and return a new series.
//
REBSER *Entab_Unicode(REBUNI *bp, REBCNT index, REBCNT len, REBINT tabsize)
{
    REBINT n = 0;
    REBUNI *dp;
    REBUNI c;

    dp = (REBUNI *)Reset_Buffer(BUF_MOLD, len);

    for (; index < len; index++) {

        c = bp[index];

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
            *dp++ = (REBYTE)c;
            n = 0;
        }
        else {
            // Incomplete tab space, pad with spaces:
            for (; n > 0; n--) *dp++ = ' ';

            // Copy chars thru end-of-line (or end of buffer):
            while (index < len) {
                if ((*dp++ = bp[index++]) == '\n') break;
            }
        }
    }

    return Copy_Buffer(BUF_MOLD, dp);
}


//
//  Detab_Bytes: C
// 
// Detab a string and return a new series.
//
REBSER *Detab_Bytes(REBYTE *bp, REBCNT index, REBCNT len, REBINT tabsize)
{
    REBCNT cnt = 0;
    REBCNT n;
    REBYTE *dp;
    REBYTE c;

    // Estimate new length based on tab expansion:
    for (n = index; n < len; n++)
        if (bp[n] == TAB) cnt++;

    dp = Reset_Buffer(BUF_FORM, len + (cnt * (tabsize-1)));

    n = 0;
    while (index < len) {

        c = bp[index++];

        if (c == '\t') {
            *dp++ = ' ';
            n++;
            for (; n % tabsize != 0; n++) *dp++ = ' ';
            continue;
        }

        if (c == '\n') n = 0;
        else n++;

        *dp++ = c;
    }

    return Copy_Buffer(BUF_FORM, dp);
}


//
//  Detab_Unicode: C
// 
// Detab a unicode string and return a new series.
//
REBSER *Detab_Unicode(REBUNI *bp, REBCNT index, REBCNT len, REBINT tabsize)
{
    REBCNT cnt = 0;
    REBCNT n;
    REBUNI *dp;
    REBUNI c;

    // Estimate new length based on tab expansion:
    for (n = index; n < len; n++)
        if (bp[n] == TAB) cnt++;

    dp = (REBUNI *)Reset_Buffer(BUF_MOLD, len + (cnt * (tabsize-1)));

    n = 0;
    while (index < len) {

        c = bp[index++];

        if (c == '\t') {
            *dp++ = ' ';
            n++;
            for (; n % tabsize != 0; n++) *dp++ = ' ';
            continue;
        }

        if (c == '\n') n = 0;
        else n++;

        *dp++ = c;
    }

    return Copy_Buffer(BUF_MOLD, dp);
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

    *out = *val;

    if (IS_CHAR(val)) {
        REBUNI c = VAL_CHAR(val);
        if (c < UNICODE_CASES) {
            c = upper ? UP_CASE(c) : LO_CASE(c);
        }
        VAL_CHAR(out) = c;
        return;
    }

    // String series:

    FAIL_IF_PROTECTED_SERIES(VAL_SERIES(val));

    len = Partial(val, 0, part, 0);
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
REBSER *Split_Lines(REBVAL *val)
{
    REBSER *ser = BUF_EMIT; // GC protected (because it is emit buffer)
    REBSER *str = VAL_SERIES(val);
    REBCNT len = VAL_LEN(val);
    REBCNT idx = VAL_INDEX(val);
    REBCNT start = idx;
    REBSER *out;
    REBUNI c;

    BLK_RESET(ser);

    while (idx < len) {
        c = GET_ANY_CHAR(str, idx);
        if (c == LF || c == CR) {
            out = Copy_String(str, start, idx - start);
            val = Alloc_Tail_Array(ser);
            Val_Init_String(val, out);
            VAL_SET_OPT(val, OPT_VALUE_LINE);
            idx++;
            if (c == CR && GET_ANY_CHAR(str, idx) == LF)
                idx++;
            start = idx;
        }
        else idx++;
    }
    // Possible remainder (no terminator)
    if (idx > start) {
        out = Copy_String(str, start, idx - start);
        val = Alloc_Tail_Array(ser);
        Val_Init_String(val, out);
        VAL_SET_OPT(val, OPT_VALUE_LINE);
    }

    return Copy_Array_Shallow(ser);
}
