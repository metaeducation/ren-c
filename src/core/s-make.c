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
Binary* Make_Binary(REBLEN capacity)
{
    Binary* bin = cast(Binary*, Make_Flex(capacity + 1, sizeof(Byte)));
    Term_Binary(bin);
    return bin;
}


//
//  Make_String: C
//
// Make a unicode string series. Add 1 extra to capacity for terminator.
//
String* Make_String(REBLEN capacity)
{
    String* str = cast(String*, Make_Flex(capacity + 1, sizeof(Ucs2Unit)));
    Term_Non_Array_Flex(str);
    return str;
}


//
//  Copy_Bytes: C
//
// Create a string series from the given bytes.
// Source is always latin-1 valid. Result is always 8bit.
//
Binary* Copy_Bytes(const Byte *src, Size size)
{
    Binary* dst = Make_Binary(size);
    memcpy(Binary_Head(dst), src, size);
    Term_Non_Array_Flex_Len(dst, size);

    return dst;
}


//
//  Insert_Char: C
//
// Insert a unicode char into a string.
//
void Insert_Char(Flex* dst, REBLEN index, REBLEN chr)
{
    if (index > Flex_Len(dst))
        index = Flex_Len(dst);
    Expand_Flex(dst, index, 1);
    SET_ANY_CHAR(dst, index, chr);
}


//
//  Copy_String_At_Len: C
//
// !!! With UTF-8 Everywhere, copying strings will still be distinct from
// other series due to the length being counted in characters and not
// units of the series width.
//
Flex* Copy_String_At_Len(const Cell* src, REBINT limit)
{
    REBLEN length_limit;
    Size size = VAL_SIZE_LIMIT_AT(&length_limit, src, limit);
    assert(length_limit * 2 == size); // !!! Temporary

    String* dst = Make_String(size / 2);
    memcpy(String_At(dst, 0), Cell_String_At(src), size);
    Term_Non_Array_Flex_Len(dst, length_limit);

    return dst;
}


//
//  Append_Unencoded_Len: C
//
// Append unencoded data to a byte string, using plain memcpy().  If dst is
// nullptr, a new byte-sized series will be created and returned.
//
Binary* Append_Unencoded_Len(Binary* dst, const char *src, REBLEN len)
{
    REBLEN tail;
    if (dst == nullptr) {
        dst = Make_Binary(len);
        tail = 0;
    }
    else {
        tail = Flex_Len(dst);
        Expand_Flex_Tail(dst, len);
    }

    memcpy(Binary_At(dst, tail), src, len);
    Term_Non_Array_Flex(dst);
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
Binary* Append_Unencoded(Binary* dst, const char *src)
{
    return Append_Unencoded_Len(dst, src, strlen(src));
}


//
//  Append_String_Ucs2Unit: C
//
// Append a non-encoded character to a string.
//
String* Append_String_Ucs2Unit(String* dst, Ucs2Unit codepoint)
{
    assert(Flex_Wide(dst) == sizeof(Ucs2Unit)); // invariant for "Latin1 Nowhere"

    REBLEN tail = Flex_Len(dst);
    Expand_Flex_Tail(dst, 1);

    Ucs2(*) cp = String_At(dst, tail);
    cp = Write_Codepoint(cp, codepoint);
    cp = Write_Codepoint(cp, '\0'); // should always be capacity for terminator

    return dst;
}


//
//  Append_Codepoint: C
//
// Encode a codepoint onto a UTF-8 binary series.
//
Binary* Append_Codepoint(Binary* dst, Codepoint codepoint)
{
    assert(Flex_Wide(dst) == sizeof(Byte));

    REBLEN tail = Flex_Len(dst);
    Expand_Flex_Tail(dst, 4); // !!! Conservative, assume long codepoint
    tail += Encode_UTF8_Char(Binary_At(dst, tail), codepoint); // 1 to 4 bytes
    Term_Binary_Len(dst, tail);
    return dst;
}


//
//  Make_Codepoint_String: C
//
// Create a string that holds a single codepoint.
//
String* Make_Codepoint_String(REBLEN codepoint)
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
void Append_Utf8_Utf8(Binary* dst, const char *utf8, size_t size)
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
void Append_Utf8_String(Binary* dst, const Cell* src, REBLEN length_limit)
{
    Size offset;
    Size size;
    Binary* temp = Temp_UTF8_At_Managed(&offset, &size, src, length_limit);

    REBLEN tail = Binary_Len(dst);
    Expand_Flex(dst, tail, size);  // tail changed too

    memcpy(Binary_At(dst, tail), Binary_At(temp, offset), size);
}


//
//  Append_Int: C
//
// Append an integer string.
//
void Append_Int(Binary* dst, REBINT num)
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
void Append_Int_Pad(Binary* dst, REBINT num, REBINT digs)
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
String* Append_UTF8_May_Fail(
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

    Resize_Flex(temp, size + 1); // needs at most this many unicode chars

    Ucs2Unit* up = String_Head(temp);
    const Byte *src = cb_cast(utf8);

    bool all_ascii = true;

    REBLEN num_codepoints = 0;

    Size bytes_left = size; // see remarks on Back_Scan_UTF8_Char's 3rd arg
    for (; bytes_left > 0; --bytes_left, ++src) {
        Ucs2Unit ch = *src;
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
        old_len = Flex_Len(dst);
        Expand_Flex_Tail(dst, num_codepoints);
    }

    Ucs2(*) dp = String_At(dst, old_len);
    Set_Flex_Len(dst, old_len + num_codepoints); // counted down to 0 below

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
Binary* Join_Binary(const Value* blk, REBINT limit)
{
    Binary* series = BYTE_BUF;

    REBLEN tail = 0;

    if (limit < 0)
        limit = Cell_Series_Len_At(blk);

    Set_Flex_Len(series, 0);

    Cell* val;
    for (val = Cell_List_At(blk); limit > 0; val++, limit--) {
        switch (Type_Of(val)) {
        case TYPE_INTEGER:
            if (VAL_INT64(val) > 255 || VAL_INT64(val) < 0)
                fail (Error_Out_Of_Range(KNOWN(val)));
            Expand_Flex_Tail(series, 1);
            *Binary_At(series, tail) = (Byte)VAL_INT32(val);
            break;

        case TYPE_BINARY: {
            REBLEN len = Cell_Series_Len_At(val);
            Expand_Flex_Tail(series, len);
            memcpy(Binary_At(series, tail), Cell_Blob_At(val), len);
            break; }

        case TYPE_TEXT:
        case TYPE_FILE:
        case TYPE_EMAIL:
        case TYPE_URL:
        case TYPE_TAG: {
            REBLEN val_len = Cell_Series_Len_At(val);
            size_t val_size = Size_As_UTF8(Cell_String_At(val), val_len);

            Expand_Flex_Tail(series, val_size);
            Set_Flex_Len(
                series,
                tail + Encode_UTF8(
                    Binary_At(series, tail),
                    val_size,
                    Cell_String_At(val),
                    &val_len
                )
            );
            break; }

        case TYPE_CHAR: {
            Expand_Flex_Tail(series, 6);
            REBLEN len =
                Encode_UTF8_Char(Binary_At(series, tail), VAL_CHAR(val));
            Set_Flex_Len(series, tail + len);
            break; }

        default:
            fail (Error_Invalid_Core(val, VAL_SPECIFIER(blk)));
        }

        tail = Flex_Len(series);
    }

    *Binary_At(series, tail) = 0;

    return series;  // SHARED FORM SERIES!
}
