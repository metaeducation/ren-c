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
// Make a byte series of length 0 with the given capacity.  The length will
// be increased by one in order to allow for a null terminator.  Binaries are
// given enough capacity to have a null terminator in case they are aliased
// as UTF-8 data later, e.g. `as word! binary`, since it would be too late
// to give them that capacity after-the-fact to enable this.
//
REBSER *Make_Binary(REBLEN capacity)
{
    REBSER *bin = Make_Ser(capacity + 1, sizeof(Byte));
    TERM_SEQUENCE(bin);
    return bin;
}


//
//  Make_String: C
//
// Make a unicode string series. Add 1 extra to capacity for terminator.
//
String* Make_String(REBLEN capacity)
{
    REBSER *ser = Make_Ser(capacity + 1, sizeof(REBUNI));
    TERM_SEQUENCE(ser);
    return ser;
}


//
//  Copy_Bytes: C
//
// Create a string series from the given bytes.
// Source is always latin-1 valid. Result is always 8bit.
//
REBSER *Copy_Bytes(const Byte *src, REBINT len)
{
    if (len < 0)
        len = LEN_BYTES(src);

    REBSER *dst = Make_Binary(len);
    memcpy(Binary_Head(dst), src, len);
    TERM_SEQUENCE_LEN(dst, len);

    return dst;
}


//
//  Insert_Char: C
//
// Insert a unicode char into a string.
//
void Insert_Char(REBSER *dst, REBLEN index, REBLEN chr)
{
    if (index > SER_LEN(dst))
        index = SER_LEN(dst);
    Expand_Series(dst, index, 1);
    SET_ANY_CHAR(dst, index, chr);
}


//
//  Copy_String_At_Len: C
//
// !!! With UTF-8 Everywhere, copying strings will still be distinct from
// other series due to the length being counted in characters and not
// units of the series width.
//
REBSER *Copy_String_At_Len(const Cell* src, REBINT limit)
{
    REBLEN length_limit;
    REBSIZ size = VAL_SIZE_LIMIT_AT(&length_limit, src, limit);
    assert(length_limit * 2 == size); // !!! Temporary

    String* dst = Make_String(size / 2);
    memcpy(AS_REBUNI(String_At(dst, 0)), Cell_String_At(src), size);
    TERM_SEQUENCE_LEN(dst, length_limit);

    return dst;
}


//
//  Append_Unencoded_Len: C
//
// Append unencoded data to a byte string, using plain memcpy().  If dst is
// nullptr, a new byte-sized series will be created and returned.
//
REBSER *Append_Unencoded_Len(REBSER *dst, const char *src, REBLEN len)
{
    REBLEN tail;
    if (dst == nullptr) {
        dst = Make_Binary(len);
        tail = 0;
    }
    else {
        tail = SER_LEN(dst);
        EXPAND_SERIES_TAIL(dst, len);
    }

    assert(BYTE_SIZE(dst));

    memcpy(Binary_At(dst, tail), src, len);
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
// Append a non-encoded character to a string.
//
REBSER *Append_Codepoint(REBSER *dst, REBUNI codepoint)
{
    assert(SER_WIDE(dst) == sizeof(REBUNI)); // invariant for "Latin1 Nowhere"

    REBLEN tail = SER_LEN(dst);
    EXPAND_SERIES_TAIL(dst, 1);

    Ucs2(*) cp = String_At(dst, tail);
    cp = Write_Codepoint(cp, codepoint);
    cp = Write_Codepoint(cp, '\0'); // should always be capacity for terminator

    return dst;
}


//
//  Append_Utf8_Codepoint: C
//
// Encode a codepoint onto a UTF-8 binary series.
//
REBSER *Append_Utf8_Codepoint(REBSER *dst, uint32_t codepoint)
{
    assert(SER_WIDE(dst) == sizeof(Byte));

    REBLEN tail = SER_LEN(dst);
    EXPAND_SERIES_TAIL(dst, 4); // !!! Conservative, assume long codepoint
    tail += Encode_UTF8_Char(Binary_At(dst, tail), codepoint); // 1 to 4 bytes
    TERM_BIN_LEN(dst, tail);
    return dst;
}


//
//  Make_Ser_Codepoint: C
//
// Create a string that holds a single codepoint.
//
REBSER *Make_Ser_Codepoint(REBLEN codepoint)
{
    assert(codepoint < (1 << 16));

    String* out = Make_String(1);
    *String_Head(out) = codepoint;
    Term_String_Len(out, 1);

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
//  Append_Utf8_String: C
//
// Append a partial string to a UTF-8 binary series.
//
// !!! Used only with mold series at the moment.
//
void Append_Utf8_String(REBSER *dst, const Cell* src, REBLEN length_limit)
{
    assert(
        SER_WIDE(dst) == sizeof(Byte)
        && SER_WIDE(VAL_SERIES(src)) == sizeof(REBUNI)
    );

    REBSIZ offset;
    REBSIZ size;
    Binary* temp = Temp_UTF8_At_Managed(&offset, &size, src, length_limit);

    REBLEN tail = SER_LEN(dst);
    Expand_Series(dst, tail, size); // tail changed too

    memcpy(Binary_At(dst, tail), Binary_At(temp, offset), size);
}


//
//  Append_Int: C
//
// Append an integer string.
//
void Append_Int(REBSER *dst, REBINT num)
{
    Byte buf[32];

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
    Byte buf[32];
    if (digs > 0)
        Form_Int_Pad(buf, num, digs, -digs, '0');
    else
        Form_Int_Pad(buf, num, -digs, digs, '0');

    Append_Unencoded(dst, s_cast(buf));
}



//
//  Append_UTF8_May_Fail: C
//
// Append UTF-8 data to a series underlying an ANY-STRING!.
//
// `dst = nullptr` means make a new string.
//
REBSER *Append_UTF8_May_Fail(
    String* dst,
    const char *utf8,
    size_t size,
    bool crlf_to_lf
){
    // This routine does not just append bytes blindly because:
    //
    // * We want to check for invalid codepoints (this can be called with
    //   arbitrary outside data from the API.
    // * It's needed to know how many characters (length) are in the series,
    //   not just how many bytes.  The higher level concept of "length" gets
    //   stored in the series LINK() field.
    // * In the future, some operations will be accelerated by knowing that
    //   a string only contains ASCII codepoints.

    String* temp = BUF_UCS2; // buffer is Unicode width

    Resize_Series(temp, size + 1); // needs at most this many unicode chars

    REBUNI *up = String_Head(temp);
    const Byte *src = cb_cast(utf8);

    bool all_ascii = true;

    REBLEN num_codepoints = 0;

    REBSIZ bytes_left = size; // see remarks on Back_Scan_UTF8_Char's 3rd arg
    for (; bytes_left > 0; --bytes_left, ++src) {
        REBUNI ch = *src;
        if (ch >= 0x80) {
            src = Back_Scan_UTF8_Char(&ch, src, &bytes_left);
            if (src == nullptr)
                fail (Error_Bad_Utf8_Raw());

            all_ascii = false;
        }
        else if (ch == CR && crlf_to_lf) {
            if (src[1] == LF)
                continue; // skip the CR, do the decrement and get the LF
            ch = LF;
        }

        ++num_codepoints;
        *up++ = ch;
    }

    up = String_Head(temp);

    REBLEN old_len;
    if (dst == nullptr) {
        dst = Make_String(num_codepoints);
        old_len = 0;
    }
    else {
        old_len = SER_LEN(dst);
        EXPAND_SERIES_TAIL(dst, num_codepoints);
    }

    REBUNI *dp = AS_REBUNI(String_At(dst, old_len));
    SET_SERIES_LEN(dst, old_len + num_codepoints); // counted down to 0 below

    for (; num_codepoints > 0; --num_codepoints)
        *dp++ = *up++;

    *dp = '\0';

    UNUSED(all_ascii);

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
REBSER *Join_Binary(const Value* blk, REBINT limit)
{
    REBSER *series = BYTE_BUF;

    REBLEN tail = 0;

    if (limit < 0)
        limit = VAL_LEN_AT(blk);

    SET_SERIES_LEN(series, 0);

    Cell* val;
    for (val = Cell_Array_At(blk); limit > 0; val++, limit--) {
        switch (VAL_TYPE(val)) {
        case REB_INTEGER:
            if (VAL_INT64(val) > 255 || VAL_INT64(val) < 0)
                fail (Error_Out_Of_Range(KNOWN(val)));
            EXPAND_SERIES_TAIL(series, 1);
            *Binary_At(series, tail) = (Byte)VAL_INT32(val);
            break;

        case REB_BINARY: {
            REBLEN len = VAL_LEN_AT(val);
            EXPAND_SERIES_TAIL(series, len);
            memcpy(Binary_At(series, tail), Cell_Binary_At(val), len);
            break; }

        case REB_TEXT:
        case REB_FILE:
        case REB_EMAIL:
        case REB_URL:
        case REB_TAG: {
            REBLEN val_len = VAL_LEN_AT(val);
            size_t val_size = Size_As_UTF8(Cell_String_At(val), val_len);

            EXPAND_SERIES_TAIL(series, val_size);
            SET_SERIES_LEN(
                series,
                tail + Encode_UTF8(
                    Binary_At(series, tail),
                    val_size,
                    Cell_String_At(val),
                    &val_len
                )
            );
            break; }

        case REB_CHAR: {
            EXPAND_SERIES_TAIL(series, 6);
            REBLEN len =
                Encode_UTF8_Char(Binary_At(series, tail), VAL_CHAR(val));
            SET_SERIES_LEN(series, tail + len);
            break; }

        default:
            fail (Error_Invalid_Core(val, VAL_SPECIFIER(blk)));
        }

        tail = SER_LEN(series);
    }

    *Binary_At(series, tail) = 0;

    return series;  // SHARED FORM SERIES!
}
