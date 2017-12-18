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
//  Insert_Char: C
//
// Insert a unicode char into a string.
//
void Insert_Char(REBSER *dst, REBCNT index, REBCNT chr)
{
    if (index > SER_LEN(dst))
        index = SER_LEN(dst);
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
){
    assert(idx <= SER_LEN(dst));

    if (NOT(no_expand))
        Expand_Series(dst, idx, len); // tail changed too

    // Src and dst have same width (8 or 16).  Currently that means BINARY!
    // inserted in BINARY! or STRING! inserted in STRING!
    //
    if (SER_WIDE(dst) == SER_WIDE(src)) {
        if (BYTE_SIZE(dst))
            memcpy(BIN_AT(dst, idx), BIN_AT(src, pos), len);
        else
            memcpy(UNI_AT(dst, idx), UNI_AT(src, pos), sizeof(REBUNI) * len);
        return;
    }

    // Src is 8 and dst is 16.  This only happens at the moment when inserting
    // BINARY! into STRING!  Should this be a UTF8 decoding?  :-/
    //
    // !!! Not implemented yet.
    //
    if (!BYTE_SIZE(dst)) {
        REBUNI *up = UNI_AT(dst, idx);
        REBYTE *bp = BIN_AT(src, pos);

        REBCNT n;
        for (n = 0; n < len; n++) {
            if (bp[n] > 127)
                fail ("BINARY! insert high-bit UTF-8 encoding into STRING!");
            up[n] = cast(REBUNI, bp[n]);
        }
        return;
    }

    // Src is 16 and dst is 8.  This only happens when inserting strings
    // into binaries, and needs a UTF8 encoding.
    //
    // !!! Not implemented yet.
    //
    REBYTE *bp = BIN_AT(dst, idx);
    REBUNI *up = UNI_AT(src, pos);

    REBCNT n;
    for (n = 0; n < len; n++) {
        if (up[n] > 127)
            fail ("Inserting non-ASCII codepoint into BINARY! from STRING!");
        bp[n] = cast(REBYTE, up[n]);
    }
}


//
//  Copy_String_At_Len: C
//
// !!! With UTF-8 Everywhere, copying strings will still be distinct from
// other series due to the length being counted in characters and not
// units of the series width.
//
REBSER *Copy_String_At_Len(REBSER *src, REBCNT index, REBINT length)
{
#if !defined(NDEBUG)
    if (SER_WIDE(src) != sizeof(REBUNI))
        panic (src);
#endif

    if (length < 0)
        length = SER_LEN(src) - index;

    REBSER *dst = Make_Unicode(length);
    Insert_String(dst, 0, src, index, length, TRUE);
    TERM_SEQUENCE_LEN(dst, length);

    return dst;
}


//
//  Append_Unencoded_Len: C
//
// Append unencoded data to a byte string, using plain memcpy().  If dst is
// NULL, a new byte-sized series will be created and returned.
//
REBSER *Append_Unencoded_Len(REBSER *dst, const char *src, REBCNT len)
{
    assert(BYTE_SIZE(dst));

    REBCNT tail;
    if (dst == NULL) {
        dst = Make_Binary(len);
        tail = 0;
    }
    else {
        tail = SER_LEN(dst);
        EXPAND_SERIES_TAIL(dst, len);
    }

    memcpy(BIN_AT(dst, tail), src, len);
    TERM_SEQUENCE(dst);
    return dst;
}


//
//  Append_Unencoded: C
//
// Append_Unencoded_Len() variant that looks for a terminating 0 byte to
// determine the length.
//
// !!! Should be in a header file so it can be inlined.
//
REBSER *Append_Unencoded(REBSER *dst, const char *src)
{
    return Append_Unencoded_Len(dst, src, strlen(src));
}


//
//  Append_Codepoint: C
//
// Optimized function to append a non-encoded character.
//
REBSER *Append_Codepoint(REBSER *dst, REBCNT codepoint)
{
    assert(SER_WIDE(dst) == sizeof(REBUNI)); // invariant for "Latin1 Nowhere"
    assert(codepoint < (1 << 16)); // not supported...yet

    REBCNT tail = SER_LEN(dst);
    EXPAND_SERIES_TAIL(dst, 1);
    *UNI_AT(dst, tail) = cast(REBUNI, codepoint);
    TERM_UNI_LEN(dst, tail + 1);
    return dst;
}


//
//  Append_Utf8_Codepoint: C
//
// Encode a codepoint onto a UTF-8 binary series.
//
REBSER *Append_Utf8_Codepoint(REBSER *dst, u32 codepoint)
{
    assert(SER_WIDE(dst) == sizeof(REBYTE));

    REBCNT tail = SER_LEN(dst);
    EXPAND_SERIES_TAIL(dst, 4); // !!! Conservative, assume long codepoint
    tail += Encode_UTF8_Char(BIN_AT(dst, tail), codepoint); // 1 to 4 bytes
    TERM_BIN_LEN(dst, tail);
    return dst;
}


//
//  Make_Series_Codepoint: C
//
// Create a string that holds a single codepoint.
//
REBSER *Make_Series_Codepoint(REBCNT codepoint)
{
    assert(codepoint < (1 << 16));

    REBSER *out = Make_Unicode(1);
    *UNI_HEAD(out) = codepoint;
    TERM_UNI_LEN(out, 1);

    return out;
}


//
//  Append_Utf8_Utf8: C
//
// Append a UTF8 byte series to a UTF8 binary.  Terminates.
//
// !!! Currently does the same thing as Append_Unencoded_Len.  Should it
// check the bytes to make sure they're actually UTF8?
//
void Append_Utf8_Utf8(REBSER *dst, const char *utf8, size_t size)
{
    Append_Unencoded_Len(dst, utf8, size);
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
// Append (or create) decoded UTF8 to a string.
//
// dst = null means make a new string.
//
REBSER *Append_UTF8_May_Fail(REBSER *dst, const char *utf8, size_t size)
{
    REBSER *temp = BUF_UTF8; // buffer is Unicode width

    Resize_Series(temp, size + 1); // needs at most this many unicode chars

    const REBOOL crlf_to_lf = FALSE;
    REBINT len = Decode_UTF8_Negative_If_ASCII(
        UNI_HEAD(temp),
        cb_cast(utf8),
        size,
        crlf_to_lf
    );

    const REBUNI *up = UNI_HEAD(temp);

    // !!! Previously it was interesting to know if all characters being
    // added were Latin1.  In the post-UTF8-everywhere world, the interesting
    // thing will be caching if they're all ASCII (for quick indexing)
    //
    if (len < 0)
        len = -len;

    REBCNT old_len;
    if (dst == NULL) {
        dst = Make_Unicode(len);
        old_len = 0;
    }
    else {
        old_len = SER_LEN(dst);
        EXPAND_SERIES_TAIL(dst, len);
    }

    REBUNI *dp = UNI_AT(dst, old_len);
    SET_SERIES_LEN(dst, old_len + len); // len is counted down to 0 below

    for (; len > 0; len--)
        *dp++ = *up++;

    *dp = '\0';

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
            if (VAL_INT64(val) > cast(i64, 255) || VAL_INT64(val) < 0)
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
            REBCNT val_len = VAL_LEN_AT(val);
            size_t val_size = Size_As_UTF8(
                VAL_UNI_AT(val), val_len, OPT_ENC_0
            );

            EXPAND_SERIES_TAIL(series, val_size);
            SET_SERIES_LEN(
                series,
                tail + Encode_UTF8(
                    BIN_AT(series, tail),
                    val_size,
                    VAL_UNI_AT(val),
                    &val_len,
                    OPT_ENC_0
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
            fail (Error_Invalid_Arg_Core(val, VAL_SPECIFIER(blk)));
        }

        tail = SER_LEN(series);
    }

    *BIN_AT(series, tail) = 0;

    return series;  // SHARED FORM SERIES!
}
