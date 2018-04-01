//
//  File: %s-make.c
//  Summary: "binary and unicode string support"
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
//  Make_Binary: C
//
// Make a binary string series. For byte, C, and UTF8 strings.
// Add 1 extra for terminator.
//
REBSER *Make_Binary(REBCNT length)
{
    REBSER *series = Make_Series(length + 1, sizeof(REBYTE));

    // !!! Clients seem to have different expectations of if `length` is
    // total capacity (and the binary should be empty) or actually is
    // specifically being preallocated at a fixed length.  Until this
    // is straightened out, terminate for both possibilities.

    BIN_HEAD(series)[length] = 0;
    TERM_SEQUENCE(series);
    return series;
}


//
//  Make_Unicode: C
//
// Make a unicode string series. Used for internal strings.
// Add 1 extra for terminator.
//
REBSER *Make_Unicode(REBCNT length)
{
    REBSER *series = Make_Series(length + 1, sizeof(REBUNI));

    // !!! Clients seem to have different expectations of if `length` is
    // total capacity (and the binary should be empty) or actually is
    // specifically being preallocated at a fixed length.  Until this
    // is straightened out, terminate for both possibilities.

    UNI_HEAD(series)[length] = 0;
    TERM_SEQUENCE(series);
    return series;
}


//
//  Copy_Bytes: C
//
// Create a string series from the given bytes.
// Source is always latin-1 valid. Result is always 8bit.
//
REBSER *Copy_Bytes(const REBYTE *src, REBINT len)
{
    if (len < 0)
        len = LEN_BYTES(src);

    REBSER *dst = Make_Binary(len);
    memcpy(BIN_HEAD(dst), src, len);
    TERM_SEQUENCE_LEN(dst, len);

    return dst;
}


//
//  Copy_Bytes_To_Unicode: C
//
// Convert a byte string to a unicode string. This can
// be used for ASCII or LATIN-8 strings.
//
REBSER *Copy_Bytes_To_Unicode(REBYTE *src, REBINT len)
{
    REBSER *series = Make_Unicode(len);
    REBUNI *dst = UNI_HEAD(series);

    for (; len > 0; len--)
        *dst++ = cast(REBUNI, *src++);

    TERM_UNI_LEN(series, len);
    return series;
}


//
//  Copy_Wide_Str: C
//
// Create a REBOL string series from a wide char string.
// Minimize to bytes if possible
//
REBSER *Copy_Wide_Str(const REBWCHAR *ws, REBINT len)
{
    assert(sizeof(REBUNI) == sizeof(REBWCHAR));

    REBSER *dst;
    if (All_Codepoints_Latin1(cast(const REBUNI*, ws), len)) {
        dst = Make_Unicode(len);
        SET_SERIES_LEN(dst, len);

        REBUNI *up = UNI_HEAD(dst);
        while (len-- > 0)
            *up++ = *ws++;
        *up = 0;
    }
    else {
        dst = Make_Binary(len);
        SET_SERIES_LEN(dst, len);

        REBYTE *bp = BIN_HEAD(dst);
        while (len-- > 0)
            *bp++ = cast(REBYTE, *ws++);
        *bp = 0;
    }
    ASSERT_SERIES_TERM(dst);
    return dst;
}


//
//  Copy_OS_Str: C
//
// Create a REBOL string series from an OS native string.
//
// For example, in Win32 with the wide char interface, we must
// convert wide char strings, minimizing to bytes if possible.
//
// For Linux the char string could be UTF-8, so that must be
// converted to REBOL Unicode or Latin byte strings.
//
REBSER *Copy_OS_Str(const void *src, REBINT len)
{
#ifdef OS_WIDE_CHAR
    return Copy_Wide_Str(cast(const REBWCHAR*, src), len);
#else
    return Decode_UTF_String(cast(const REBYTE*, src), len, 8);
#endif
}


//
//  Insert_Char: C
//
// Insert a Char (byte or unicode) into a string.
//
void Insert_Char(REBSER *dst, REBCNT index, REBCNT chr)
{
    if (index > SER_LEN(dst)) index = SER_LEN(dst);
    if (chr > 0xFF && BYTE_SIZE(dst)) Widen_String(dst, TRUE);
    Expand_Series(dst, index, 1);
    SET_ANY_CHAR(dst, index, chr);
}


//
//  Insert_String: C
//
// Insert a non-encoded string into a series at given index.
// Source and/or destination can be 1 or 2 bytes wide.
// If destination is not wide enough, it will be widened.
//
void Insert_String(
    REBSER *dst,
    REBCNT idx,
    REBSER *src,
    REBCNT pos,
    REBCNT len,
    REBOOL no_expand
) {
    REBUNI *up;
    REBYTE *bp;
    REBCNT n;

    assert(idx <= SER_LEN(dst));

    if (!no_expand) Expand_Series(dst, idx, len); // tail changed too

    // Src and dst have same width (8 or 16):
    if (SER_WIDE(dst) == SER_WIDE(src)) {
cp_same:
        if (BYTE_SIZE(dst))
            memcpy(BIN_AT(dst, idx), BIN_AT(src, pos), len);
        else
            memcpy(UNI_AT(dst, idx), UNI_AT(src, pos), sizeof(REBUNI) * len);
        return;
    }

    // Src is 8 and dst is 16:
    if (!BYTE_SIZE(dst)) {
        bp = BIN_AT(src, pos);
        up = UNI_AT(dst, idx);
        for (n = 0; n < len; n++) up[n] = (REBUNI)bp[n];
        return;
    }

    // Src is 16 and dst is 8:
    bp = BIN_AT(dst, idx);
    up = UNI_AT(src, pos);
    for (n = 0; n < len; n++) {
        if (up[n] > 0xFF) {
            // Expand dst and restart:
            idx += n;
            pos += n;
            len -= n;
            Widen_String(dst, TRUE);
            goto cp_same;
        }
        bp[n] = (REBYTE)up[n];
    }
}


//
//  Copy_String_Slimming: C
//
// Copies a portion of any string (byte or unicode).  If the input is a
// wide REBUNI string, the range of copied characters will be examined to
// see if they could fit in a byte-size series.  The string will be
// "slimmed" if possible.
//
REBSER *Copy_String_Slimming(REBSER *src, REBCNT index, REBINT length)
{
    REBYTE wide = 1;

    if (length < 0)
        length = SER_LEN(src) - index;

    // Can it be slimmed down?
    if (!BYTE_SIZE(src)) {
        REBUNI *up = UNI_AT(src, index);

        REBINT n;
        for (n = 0; n < length; n++)
            if (up[n] > 0xff)
                break;

        if (n < length)
            wide = sizeof(REBUNI);
    }

    REBSER *dst = Make_Series(length + 1, wide);
    Insert_String(dst, 0, src, index, length, TRUE);
    TERM_SEQUENCE_LEN(dst, length);

    return dst;
}


//
//  Val_Str_To_OS_Managed: C
//
// This is used to pass a REBOL value string to an OS API.
//
// The REBOL (input) string can be byte or wide sized.
// The OS (output) string is in the native OS format.
// On Windows, its a wide-char, but on Linux, its UTF-8.
//
// If we know that the string can be used directly as-is,
// (because it's in the OS size format), we can used it
// like that.
//
// !!! The series is created but just let up to the garbage
// collector to free.  This is a "leaky" approach.  You may
// optionally request to have the series returned if it is
// important for you to protect it from GC, but you cannot
// currently get a "freeable" series out of this.
//
REBCHR *Val_Str_To_OS_Managed(REBSER **out, REBVAL *val)
{
#ifdef OS_WIDE_CHAR
    if (VAL_BYTE_SIZE(val)) {
        // On windows, we need to convert byte to wide:
        REBINT n = VAL_LEN_AT(val);
        REBSER *up = Make_Unicode(n);

        // !!!"Leaks" in the sense that the GC has to take care of this
        MANAGE_SERIES(up);

        n = Decode_UTF8_Negative_If_Latin1(
            UNI_HEAD(up),
            VAL_BIN_AT(val),
            n,
            FALSE
        );
        TERM_UNI_LEN(up, abs(n));

        if (out) *out = up;

        return cast(REBCHR*, UNI_HEAD(up));
    }
    else {
        // Already wide, we can use it as-is:
        // !Assumes the OS uses same wide format!

        if (out) *out = VAL_SERIES(val);

        return cast(REBCHR*, VAL_UNI_AT(val));
    }
#else
    if (
        VAL_BYTE_SIZE(val)
        && All_Bytes_ASCII(VAL_BIN_AT(val), VAL_LEN_AT(val))
    ) {
        if (out) *out = VAL_SERIES(val);

        // On Linux/Unix we can use ASCII directly (it is valid UTF-8):
        return cast(REBCHR*, VAL_BIN_AT(val));
    }
    else {
        // !!! "Leaks" in the sense that the GC has to take care of this
        REBSER *ser = Temp_UTF8_At_Managed(val, 0, NULL);

        if (out) *out = ser;

        // NOTE: may return a shared buffer!
        return cast(REBCHR*, BIN_HEAD(ser));
    }
#endif
}


//
//  Append_Unencoded_Len: C
//
// Optimized function to append a non-encoded byte string.
//
// If dst is null, it will be created and returned.
// Such src strings normally come from C code or tables.
// Destination can be 1 or 2 bytes wide.
//
REBSER *Append_Unencoded_Len(REBSER *dst, const char *src, REBCNT len)
{
    REBUNI *up;
    REBCNT tail;

    if (!dst) {
        dst = Make_Binary(len);
        tail = 0;
    } else {
        tail = SER_LEN(dst);
        EXPAND_SERIES_TAIL(dst, len);
    }

    if (BYTE_SIZE(dst)) {
        memcpy(BIN_AT(dst, tail), src, len);
        TERM_SEQUENCE(dst);
    }
    else {
        up = UNI_AT(dst, tail);
        for (; len > 0; len--) *up++ = (REBUNI)*src++;
        *up = 0;
    }

    return dst;
}


//
//  Append_Unencoded: C
//
// Optimized function to append a non-encoded byte string.
// If dst is null, it will be created and returned.
// Such src strings normally come from C code or tables.
// Destination can be 1 or 2 bytes wide.
//
REBSER *Append_Unencoded(REBSER *dst, const char *src)
{
    return Append_Unencoded_Len(dst, src, strlen(src));
}


//
//  Append_Codepoint: C
//
// Optimized function to append a non-encoded character.
// Destination can be 1 or 2 bytes wide, but DOES NOT WIDEN.
//
REBSER *Append_Codepoint(REBSER *dst, REBCNT codepoint)
{
    REBCNT tail = SER_LEN(dst);

    EXPAND_SERIES_TAIL(dst, 1);

    if (BYTE_SIZE(dst)) {
        assert(codepoint < (1 << 8));
        *BIN_AT(dst, tail) = cast(REBYTE, codepoint);
        TERM_BIN(dst);
    }
    else {
        assert(codepoint < (1 << 16));
        *UNI_AT(dst, tail) = cast(REBUNI, codepoint);
        TERM_UNI(dst);
    }

    return dst;
}


//
//  Make_Series_Codepoint: C
//
// Create a series that holds a single codepoint.  If the
// codepoint will fit into a byte, then it will be a byte
// series.  If two bytes, it will be a REBUNI series.
//
// (Codepoints greater than the size of REBUNI are not
// currently supported in Rebol3.)
//
REBSER *Make_Series_Codepoint(REBCNT codepoint)
{
    REBSER *out;

    assert(codepoint < (1 << 16));

    out = (codepoint > 255) ? Make_Unicode(1) : Make_Binary(1);
    TERM_SEQUENCE(out);

    Append_Codepoint(out, codepoint);

    return out;
}


//
//  Append_Uni_Bytes: C
//
// Append a unicode string to a byte string. OPTIMZED.
//
void Append_Uni_Bytes(REBSER *dst, const REBUNI *src, REBCNT len)
{
    REBCNT old_len = SER_LEN(dst);

    EXPAND_SERIES_TAIL(dst, len);
    SET_SERIES_LEN(dst, old_len + len);

    REBYTE *bp = BIN_AT(dst, old_len);

    for (; len > 0; len--)
        *bp++ = cast(REBYTE, *src++);

    *bp = 0;
}


//
//  Append_Uni_Uni: C
//
// Append a unicode string to a unicode string. OPTIMZED.
//
void Append_Uni_Uni(REBSER *dst, const REBUNI *src, REBCNT len)
{
    REBCNT old_len = SER_LEN(dst);

    EXPAND_SERIES_TAIL(dst, len);
    SET_SERIES_LEN(dst, old_len + len);

    REBUNI *up = UNI_AT(dst, old_len);

    for (; len > 0; len--)
        *up++ = *src++;

    *up = 0;
}


//
//  Append_String: C
//
// Append a byte or unicode string to a unicode string.
//
void Append_String(REBSER *dst, REBSER *src, REBCNT i, REBCNT len)
{
    Insert_String(dst, SER_LEN(dst), src, i, len, FALSE);
}


//
//  Append_Int: C
//
// Append an integer string.
//
void Append_Int(REBSER *dst, REBINT num)
{
    REBYTE buf[32];

    Form_Int(buf, num);
    Append_Unencoded(dst, s_cast(buf));
}


//
//  Append_Int_Pad: C
//
// Append an integer string.
//
void Append_Int_Pad(REBSER *dst, REBINT num, REBINT digs)
{
    REBYTE buf[32];
    if (digs > 0)
        Form_Int_Pad(buf, num, digs, -digs, '0');
    else
        Form_Int_Pad(buf, num, -digs, digs, '0');

    Append_Unencoded(dst, s_cast(buf));
}



//
//  Append_UTF8_May_Fail: C
//
// Append (or create) decoded UTF8 to a string. OPTIMIZED.
//
// Result can be 8 bits (latin-1 optimized) or 16 bits wide.
//
// dst = null means make a new string.
//
REBSER *Append_UTF8_May_Fail(REBSER *dst, const REBYTE *src, REBCNT num_bytes)
{
    REBSER *ser = BUF_UTF8; // buffer is Unicode width

    Resize_Series(ser, num_bytes + 1); // needs at most this many unicode chars

    REBINT len = Decode_UTF8_Negative_If_Latin1(
        UNI_HEAD(ser),
        src,
        num_bytes,
        FALSE
    );

    if (len < 0) { // All characters being added are Latin1
        len = -len;
        if (dst == NULL)
            dst = Make_Binary(len);
        if (BYTE_SIZE(dst)) {
            Append_Uni_Bytes(dst, UNI_HEAD(ser), len);
            return dst;
        }
    }
    else {
        if (dst == NULL)
            dst = Make_Unicode(len);
    }

    Append_Uni_Uni(dst, UNI_HEAD(ser), len);

    return dst;
}


//
//  Join_Binary: C
//
// Join a binary from component values for use in standard
// actions like make, insert, or append.
// limit: maximum number of values to process
// limit < 0 means no limit
//
// WARNING: returns BYTE_BUF, not a copy!
//
REBSER *Join_Binary(const REBVAL *blk, REBINT limit)
{
    REBSER *series = BYTE_BUF;

    REBCNT tail = 0;

    if (limit < 0)
        limit = VAL_LEN_AT(blk);

    SET_SERIES_LEN(series, 0);

    RELVAL *val;
    for (val = VAL_ARRAY_AT(blk); limit > 0; val++, limit--) {
        switch (VAL_TYPE(val)) {
        case REB_INTEGER:
            if (VAL_INT64(val) > 255 || VAL_INT64(val) < 0)
                fail (Error_Out_Of_Range(KNOWN(val)));
            EXPAND_SERIES_TAIL(series, 1);
            *BIN_AT(series, tail) = (REBYTE)VAL_INT32(val);
            break;

        case REB_BINARY: {
            REBCNT len = VAL_LEN_AT(val);
            EXPAND_SERIES_TAIL(series, len);
            memcpy(BIN_AT(series, tail), VAL_BIN_AT(val), len);
            break; }

        case REB_STRING:
        case REB_FILE:
        case REB_EMAIL:
        case REB_URL:
        case REB_TAG: {
            REBCNT len = VAL_LEN_AT(val);

            void *bp = VAL_BYTE_SIZE(val)
                ? VAL_BIN_AT(val)
                : (REBYTE*)VAL_UNI_AT(val);

            REBCNT bl = Length_As_UTF8(
                bp, len, VAL_BYTE_SIZE(val) ? 0 : OPT_ENC_UNISRC
            );

            EXPAND_SERIES_TAIL(series, bl);
            SET_SERIES_LEN(
                series,
                tail + Encode_UTF8(
                    BIN_AT(series, tail),
                    bl,
                    bp,
                    &len,
                    VAL_BYTE_SIZE(val) ? 0 : OPT_ENC_UNISRC
                )
            );
            break; }

        case REB_CHAR: {
            EXPAND_SERIES_TAIL(series, 6);
            REBCNT len =
                Encode_UTF8_Char(BIN_AT(series, tail), VAL_CHAR(val));
            SET_SERIES_LEN(series, tail + len);
            break; }

        default:
            fail (Error_Invalid_Core(val, VAL_SPECIFIER(blk)));
        }

        tail = SER_LEN(series);
    }

    *BIN_AT(series, tail) = 0;

    return series;  // SHARED FORM SERIES!
}
