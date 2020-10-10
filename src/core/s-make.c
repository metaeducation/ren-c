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
REBSTR *Make_String_Core(REBSIZ encoded_capacity, REBFLGS flags)
{
    REBSER *s = Make_Series_Core(
        encoded_capacity + 1,  // +1 is for NUL terminator
        sizeof(REBYTE),
        flags | SERIES_FLAG_IS_STRING | SERIES_FLAG_UTF8_NONWORD
    );
    MISC(s).length = 0;
    LINK(s).bookmarks = nullptr;  // generated on demand
    TERM_SERIES(s);
    return STR(s);
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
        len = strsize(src);

    REBSER *dst = Make_Binary(len);
    memcpy(BIN_HEAD(dst), src, len);
    TERM_SEQUENCE_LEN(dst, len);

    return dst;
}


//
//  Copy_String_At_Limit: C
//
// !!! With UTF-8 Everywhere, copying strings will still be distinct from
// other series due to the length being counted in characters and not
// units of the series width.
//
REBSTR *Copy_String_At_Limit(const RELVAL *src, REBINT limit)
{
    REBSIZ limited_size;
    REBLEN limited_length;
    REBCHR(const*) utf8 = VAL_UTF8_LEN_SIZE_AT_LIMIT(
        &limited_length,
        &limited_size,
        src,
        limit
    );

    REBSTR *dst = Make_String(limited_size);
    memcpy(STR_HEAD(dst), utf8, limited_size);
    TERM_STR_LEN_SIZE(dst, limited_length, limited_size);

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
REBSTR *Append_Codepoint(REBSTR *dst, REBUNI c)
{
    if (c == '\0') {
        assert(!"Zero byte being added to string.");  // caller should handle
        fail (Error_Illegal_Zero_Byte_Raw());  // don't crash release build
    }

    assert(c <= MAX_UNI);
    assert(not IS_STR_SYMBOL(dst));

    REBLEN old_len = STR_LEN(dst);

    REBSIZ tail = STR_SIZE(dst);
    REBSIZ encoded_size = Encoded_Size_For_Codepoint(c);
    EXPAND_SERIES_TAIL(SER(dst), encoded_size);
    Encode_UTF8_Char(BIN_AT(SER(dst), tail), c, encoded_size);

    // "length" grew by 1 codepoint, but "size" grew by 1 to UNI_MAX_ENCODED
    //
    TERM_STR_LEN_SIZE(dst, old_len + 1, tail + encoded_size);

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
REBSTR *Make_Codepoint_String(REBUNI c)
{
    if (c == '\0')
        fail (Error_Illegal_Zero_Byte_Raw());

    REBSIZ size = Encoded_Size_For_Codepoint(c);
    REBSTR *s = Make_String(size);
    Encode_UTF8_Char(STR_HEAD(s), c, size);
    TERM_STR_LEN_SIZE(s, 1, size);
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
REBSTR *Append_Ascii_Len(REBSTR *dst, const char *ascii, REBLEN len)
{
    REBLEN old_size;
    REBLEN old_len;

    if (dst == NULL) {
        dst = Make_String(len);
        old_size = 0;
        old_len = 0;
    }
    else {
        old_size = STR_SIZE(dst);
        old_len = STR_LEN(dst);
        EXPAND_SERIES_TAIL(SER(dst), len);
    }

    memcpy(BIN_AT(SER(dst), old_size), ascii, len);

    TERM_STR_LEN_SIZE(dst, old_len + len, old_size + len);
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
REBSTR *Append_Ascii(REBSTR *dst, const char *src)
{
    return Append_Ascii_Len(dst, src, strsize(src));
}


//
//  Append_Utf8: C
//
// Append a UTF8 byte series to a UTF8 binary.  Terminates.
//
REBSTR *Append_Utf8(REBSTR *dst, const char *utf8, size_t size)
{
    return Append_UTF8_May_Fail(dst, utf8, size, STRMODE_NO_CR);
}


//
//  Append_Spelling: C
//
// Append the spelling of a REBSTR to a UTF8 binary.  Terminates.
//
void Append_Spelling(REBSTR *dst, const REBSTR *spelling)
{
    Append_Utf8(dst, STR_UTF8(spelling), STR_SIZE(spelling));
}


//
//  Append_String: C
//
// Append a partial string to a UTF-8 binary series.
//
void Append_String(REBSTR *dst, REBCEL(const*) src, REBLEN limit)
{
    assert(not IS_STR_SYMBOL(dst));
    assert(ANY_UTF8_KIND(CELL_TYPE(src)));

    REBLEN len;
    REBSIZ size;
    REBCHR(const*) utf8 = VAL_UTF8_LEN_SIZE_AT_LIMIT(&len, &size, src, limit);

    REBLEN old_len = STR_LEN(dst);
    REBSIZ old_used = STR_SIZE(dst);

    REBLEN tail = STR_SIZE(dst);
    Expand_Series(SER(dst), tail, size);  // series USED changes too

    memcpy(BIN_AT(SER(dst), tail), utf8, size);
    TERM_STR_LEN_SIZE(dst, old_len + len, old_used + size);
}


//
//  Append_Int: C
//
// Append an integer string.
//
void Append_Int(REBSTR *dst, REBINT num)
{
    REBYTE buf[32];
    Form_Int(buf, num);

    Append_Ascii(dst, s_cast(buf));
}


//
//  Append_Int_Pad: C
//
// Append an integer string.
//
void Append_Int_Pad(REBSTR *dst, REBINT num, REBINT digs)
{
    REBYTE buf[32];
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
REBSTR *Append_UTF8_May_Fail(
    REBSTR *dst,  // if nullptr, that means make a new string
    const char *utf8,
    REBSIZ size,
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

    const REBYTE *bp = cb_cast(utf8);

    DECLARE_MOLD (mo); // !!! REVIEW: don't need intermediate if no CRLF_TO_LF
    Push_Mold(mo);

    bool all_ascii = true;
    REBLEN num_codepoints = 0;

    REBSIZ bytes_left = size; // see remarks on Back_Scan_UTF8_Char's 3rd arg
    for (; bytes_left > 0; --bytes_left, ++bp) {
        REBUNI c = *bp;
        if (c >= 0x80) {
            bp = Back_Scan_UTF8_Char(&c, bp, &bytes_left);
            if (bp == NULL)
                fail (Error_Bad_Utf8_Raw()); // !!! Should Back_Scan() fail?

            all_ascii = false;
        }
        else if (Should_Skip_Ascii_Byte_May_Fail(
            bp,
            strmode,
            cast(const REBYTE*, utf8)
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

    REBLEN old_len = STR_LEN(dst);
    REBSIZ old_size = STR_SIZE(dst);

    EXPAND_SERIES_TAIL(SER(dst), size);
    memcpy(
        BIN_AT(SER(dst), old_size),
        BIN_AT(SER(mo->series), mo->offset),
        STR_SIZE(mo->series) - mo->offset
    );

    TERM_STR_LEN_SIZE(
        dst,
        old_len + num_codepoints,
        old_size + STR_SIZE(mo->series) - mo->offset
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
    REBSER *buf = BYTE_BUF;

    REBLEN tail = 0;

    if (limit < 0)
        limit = VAL_LEN_AT(blk);

    SET_SERIES_LEN(buf, 0);

    const RELVAL *val;
    for (val = VAL_ARRAY_AT(blk); limit > 0; val++, limit--) {
        switch (VAL_TYPE(val)) {
        case REB_INTEGER:
            EXPAND_SERIES_TAIL(buf, 1);
            *BIN_AT(buf, tail) = cast(REBYTE, VAL_UINT8(val));  // can fail()
            break;

        case REB_BINARY: {
            REBLEN len = VAL_LEN_AT(val);
            EXPAND_SERIES_TAIL(buf, len);
            memcpy(BIN_AT(buf, tail), VAL_BIN_AT(val), len);
            break; }

        case REB_ISSUE:
        case REB_TEXT:
        case REB_FILE:
        case REB_EMAIL:
        case REB_URL:
        case REB_TAG: {
            REBSIZ utf8_size;
            REBCHR(const*) utf8 = VAL_UTF8_SIZE_AT(&utf8_size, val);

            EXPAND_SERIES_TAIL(buf, utf8_size);
            memcpy(BIN_AT(buf, tail), utf8, utf8_size);
            SET_SERIES_LEN(buf, tail + utf8_size);
            break; }

        default:
            fail (Error_Bad_Value_Core(val, VAL_SPECIFIER(blk)));
        }

        tail = SER_USED(buf);
    }

    *BIN_AT(buf, tail) = 0;
}
