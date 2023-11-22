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
// Copyright 2012-2017 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//

#include "sys-core.h"



//
//  Make_String_Core: C
//
// Makes a series to hold a string with enough capacity for a certain amount
// of encoded data.  Note that this is not a guarantee of being able to hold
// more than `encoded_capacity / UNI_ENCODED_MAX` unencoded codepoints...
//
String* Make_String_Core(Size encoded_capacity, Flags flags)
{
    assert(Flavor_From_Flags(flags) == 0);  // shouldn't have a flavor

    String* str = Make_Series(String,
        encoded_capacity + 1,  // binary includes room for '\0' terminator
        FLAG_FLAVOR(STRING) | flags
    );
    str->misc.length = 0;
    mutable_LINK(Bookmarks, str) = nullptr;  // generated on demand
    *Binary_Head(str) = '\0';  // zero length, so head = tail
    return str;
}


//
//  Copy_Bytes: C
//
// Create a string series from the given bytes.
// Source is always latin-1 valid. Result is always 8bit.
//
Binary* Copy_Bytes(const Byte* src, REBINT len)
{
    if (len < 0)
        len = strsize(src);

    Binary* bin = Make_Binary(len);
    memcpy(Binary_Head(bin), src, len);
    Term_Binary_Len(bin, len);
    return bin;
}


//
//  Copy_String_At_Limit: C
//
// !!! With UTF-8 Everywhere, copying strings will still be distinct from
// other series due to the length being counted in characters and not
// units of the series width.
//
String* Copy_String_At_Limit(const Cell* src, REBINT limit)
{
    Size limited_size;
    Length limited_length;
    Utf8(const*) utf8 = VAL_UTF8_LEN_SIZE_AT_LIMIT(
        &limited_length,
        &limited_size,
        src,
        limit
    );

    String* dst = Make_String(limited_size);
    memcpy(String_Head(dst), utf8, limited_size);
    Term_String_Len_Size(dst, limited_length, limited_size);

    return dst;
}


//
//  Append_Codepoint: C
//
// Encode a codepoint onto the end of a UTF-8 string series.  This is used
// frequently by molding.
//
// !!! Should the mold buffer avoid paying for termination?  Might one save on
// resizing checks if an invalid UTF-8 byte were used to mark the end of the
// capacity (the way END markers are used on the data stack?)
//
String* Append_Codepoint(String* dst, Codepoint c)
{
    if (c == '\0') {
        assert(!"Zero byte being added to string.");  // caller should handle
        fail (Error_Illegal_Zero_Byte_Raw());  // don't crash release build
    }

    assert(c <= MAX_UNI);
    assert(not IS_SYMBOL(dst));

    Length old_len = String_Len(dst);

    Size tail = String_Size(dst);
    Size encoded_size = Encoded_Size_For_Codepoint(c);
    Expand_Series_Tail(dst, encoded_size);
    Encode_UTF8_Char(Binary_At(dst, tail), c, encoded_size);

    // "length" grew by 1 codepoint, but "size" grew by 1 to UNI_MAX_ENCODED
    //
    Term_String_Len_Size(dst, old_len + 1, tail + encoded_size);

    return dst;
}


//
//  Make_Codepoint_String: C
//
// Create a string that holds a single codepoint.
//
// !!! This could be more optimal if a CHAR! is passed in, because it caches
// the UTF-8 encoding in the cell.  Review callsites if that is actionable.
//
String* Make_Codepoint_String(Codepoint c)
{
    if (c == '\0')
        fail (Error_Illegal_Zero_Byte_Raw());

    Size size = Encoded_Size_For_Codepoint(c);
    String* s = Make_String(size);
    Encode_UTF8_Char(String_Head(s), c, size);
    Term_String_Len_Size(s, 1, size);
    return s;
}


//
//  Append_Ascii_Len: C
//
// Append unencoded data to a byte string, using plain memcpy().  If dst is
// NULL, a new byte-sized series will be created and returned.
//
// !!! Should debug build assert it's ASCII?  Most of these are coming from
// string literals in the source.
//
String* Append_Ascii_Len(String* dst, const char *ascii, REBLEN len)
{
    REBLEN old_size;
    REBLEN old_len;

    if (dst == nullptr) {
        dst = Make_String(len);
        old_size = 0;
        old_len = 0;
    }
    else {
        old_size = String_Size(dst);
        old_len = String_Len(dst);
        Expand_Series_Tail(dst, len);
    }

    memcpy(Binary_At(dst, old_size), ascii, len);

    Term_String_Len_Size(dst, old_len + len, old_size + len);
    return dst;
}


//
//  Append_Ascii: C
//
// Append_Ascii_Len() variant that looks for a terminating 0 byte to
// determine the length.  Assumes one byte per character.
//
// !!! Should be in a header file so it can be inlined.
//
String* Append_Ascii(String* dst, const char *src)
{
    return Append_Ascii_Len(dst, src, strsize(src));
}


//
//  Append_Utf8: C
//
// Append a UTF8 byte series to a UTF8 binary.  Terminates.
//
String* Append_Utf8(String* dst, const char *utf8, size_t size)
{
    return Append_UTF8_May_Fail(dst, utf8, size, STRMODE_NO_CR);
}


//
//  Append_Spelling: C
//
// Append the spelling of a REBSTR to a UTF8 binary.  Terminates.
//
void Append_Spelling(String* dst, const String* spelling)
{
    Append_Utf8(dst, String_UTF8(spelling), String_Size(spelling));
}


//
//  Append_String_Limit: C
//
// Append a partial string to a String*.
//
void Append_String_Limit(String* dst, NoQuote(const Cell*) src, REBLEN limit)
{
    assert(not IS_SYMBOL(dst));
    assert(Any_Utf8_Kind(Cell_Heart(src)));

    Length len;
    Size size;
    Utf8(const*) utf8 = VAL_UTF8_LEN_SIZE_AT_LIMIT(&len, &size, src, limit);

    Length old_len = String_Len(dst);
    Size old_used = String_Size(dst);

    REBLEN tail = String_Size(dst);
    Expand_Series(dst, tail, size);  // series USED changes too

    memcpy(Binary_At(dst, tail), utf8, size);
    Term_String_Len_Size(dst, old_len + len, old_used + size);
}


//
//  Append_Int: C
//
// Append an integer string.
//
void Append_Int(String* dst, REBINT num)
{
    Byte buf[32];
    Form_Int(buf, num);

    Append_Ascii(dst, s_cast(buf));
}


//
//  Append_Int_Pad: C
//
// Append an integer string.
//
void Append_Int_Pad(String* dst, REBINT num, REBINT digs)
{
    Byte buf[32];
    if (digs > 0)
        Form_Int_Pad(buf, num, digs, -digs, '0');
    else
        Form_Int_Pad(buf, num, -digs, digs, '0');

    Append_Ascii(dst, s_cast(buf));
}



//
//  Append_UTF8_May_Fail: C
//
// Append UTF-8 data to a series underlying an ANY-STRING! (or create new one)
//
String* Append_UTF8_May_Fail(
    String* dst,  // if nullptr, that means make a new string
    const char *utf8,
    Size size,
    enum Reb_Strmode strmode
){
    // This routine does not just append bytes blindly because:
    //
    // * If STRMODE_CRLF_TO_LF is set, some characters may need to be removed
    // * We want to check for invalid byte sequences, as this can be called
    //   with arbitrary outside data from the API.
    // * It's needed to know how many characters (length) are in the series,
    //   not just how many bytes.  The higher level concept of "length" gets
    //   stored in the series MISC() field.
    // * In the future, some operations will be accelerated by knowing that
    //   a string only contains ASCII codepoints.

    const Byte* bp = cb_cast(utf8);

    DECLARE_MOLD (mo); // !!! REVIEW: don't need intermediate if no CRLF_TO_LF
    Push_Mold(mo);

    bool all_ascii = true;
    REBLEN num_codepoints = 0;

    Size bytes_left = size;  // see remarks on Back_Scan_UTF8_Char's 3rd arg
    for (; bytes_left > 0; --bytes_left, ++bp) {
        Codepoint c = *bp;
        if (c >= 0x80) {
            bp = Back_Scan_UTF8_Char(&c, bp, &bytes_left);
            if (bp == NULL)
                fail (Error_Bad_Utf8_Raw()); // !!! Should Back_Scan() fail?

            all_ascii = false;
        }
        else if (Should_Skip_Ascii_Byte_May_Fail(
            bp,
            strmode,
            c_cast(Byte*, utf8)
        )){
            continue;
        }

        ++num_codepoints;
        Append_Codepoint(mo->series, c);
    }

    UNUSED(all_ascii);

    // !!! The implicit nature of this is probably not the best way of
    // handling things, but... if the series we were supposed to be appending
    // to was the mold buffer, that's what we just did.  Consider making this
    // a specific call for Mold_Utf8() or similar.
    //
    if (dst == mo->series)
        return dst;

    if (not dst)
        return Pop_Molded_String(mo);

    Length old_len = String_Len(dst);
    Size old_size = String_Size(dst);

    Expand_Series_Tail(dst, size);
    memcpy(
        Binary_At(dst, old_size),
        Binary_At(mo->series, mo->base.size),
        String_Size(mo->series) - mo->base.size
    );

    Term_String_Len_Size(
        dst,
        old_len + num_codepoints,
        old_size + String_Size(mo->series) - mo->base.size
    );

    Drop_Mold(mo);

    return dst;
}


//
//  Join_Binary_In_Byte_Buf: C
//
// Join a binary from component values for use in standard
// actions like make, insert, or append.
// limit: maximum number of values to process
// limit < 0 means no limit
//
// !!! This routine uses a different buffer from molding, because molding
// currently has to maintain valid UTF-8 data.  It may be that the buffers
// should be unified.
//
void Join_Binary_In_Byte_Buf(const REBVAL *blk, REBINT limit)
{
    Binary* buf = BYTE_BUF;

    REBLEN tail = 0;

    if (limit < 0)
        limit = VAL_LEN_AT(blk);

    Set_Series_Len(buf, 0);

    const Cell* val = VAL_ARRAY_ITEM_AT(blk);
    for (; limit > 0; val++, limit--) {
        switch (VAL_TYPE(val)) {
          case REB_BLANK:
            break;

          case REB_QUASI:
            fail (Error_Bad_Value(val));

          case REB_INTEGER:
            Expand_Series_Tail(buf, 1);
            *Binary_At(buf, tail) = cast(Byte, VAL_UINT8(val));  // can fail()
            break;

          case REB_BINARY: {
            Size size;
            const Byte* data = VAL_BINARY_SIZE_AT(&size, val);
            Expand_Series_Tail(buf, size);
            memcpy(Binary_At(buf, tail), data, size);
            break; }

          case REB_ISSUE:
          case REB_TEXT:
          case REB_FILE:
          case REB_EMAIL:
          case REB_URL:
          case REB_TAG: {
            Size utf8_size;
            Utf8(const*) utf8 = VAL_UTF8_SIZE_AT(&utf8_size, val);

            Expand_Series_Tail(buf, utf8_size);
            memcpy(Binary_At(buf, tail), utf8, utf8_size);
            Set_Series_Len(buf, tail + utf8_size);
            break; }

          default:
            fail (Error_Bad_Value(val));
        }

        tail = Series_Used(buf);
    }

    *Binary_At(buf, tail) = 0;
}
