//
//  File: %mod-utf.c
//  Summary: "UTF-16 and UTF-32 Extension"
//  Section: extension
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
// See %extensions/utf/README.md
//
// This is low-priority code that was moved into an extension, so that it
// would not take up space in core builds.
//

#include "sys-core.h"

#include "tmp-mod-utf.h"


//
//  What_UTF: C
//
// Tell us what UTF encoding the byte stream has, as integer # of bits.
// 0 is unknown, negative for Little Endian.
//
// !!! Currently only uses the Byte-Order-Mark for detection (which is not
// necessarily present)
//
// !!! Note that UTF8 is not prescribed to have a byte order mark by the
// standard.  Writing routines will not add it by default, hence if it is
// present it is to be considered part of the in-band data stream...so that
// reading and writing back out will preserve the input.
//
REBINT What_UTF(const REBYTE *bp, REBLEN len)
{
    if (len >= 3 && bp[0] == 0xef && bp[1] == 0xbb && bp[2] == 0xbf)
        return 8; // UTF8 (endian agnostic)

    if (len >= 2) {
        if (bp[0] == 0xfe && bp[1] == 0xff)
            return 16; // UTF16 big endian

        if (bp[0] == 0xff && bp[1] == 0xfe) {
            if (len >= 4 && bp[2] == 0 && bp[3] == 0)
                return -32; // UTF32 little endian
            return -16; // UTF16 little endian
        }

        if (
            len >= 4
            && bp[0] == 0 && bp[1] == 0 && bp[2] == 0xfe && bp[3] == 0xff
        ){
            return 32; // UTF32 big endian
        }
    }

    return 0; // unknown
}


//
//  Decode_UTF16_Negative_If_ASCII: C
//
// src: source binary data
// len: byte-length of source (not number of chars)
// little_endian: little endian encoded
// crlf_to_lf: convert CRLF/CR to LF
//
// Returns length in chars (negative if all chars are ASCII).
// No terminator is added.
//
REBSTR *Decode_UTF16(
    const REBYTE *src,
    REBLEN len,
    bool little_endian,
    bool crlf_to_lf
){
    REBSTR *s = Make_String(len * 2);  // !!! conservative, 4 bytes per 2...

    bool expect_lf = false;
    bool ascii = true;
    REBUNI c;

    REBLEN num_chars = 0;

    REBCHR(*) dp = STR_HEAD(s);

    for (; len > 0; len--, src++) {
        //
        // Combine bytes in big or little endian format
        //
        c = *src;
        if (not little_endian)
            c <<= 8;
        if (--len <= 0)
            break;

        src++;

        c |= little_endian ? (cast(REBUNI, *src) << 8) : *src;

        if (crlf_to_lf) {
            //
            // Skip CR, but add LF (even if missing)
            //
            if (expect_lf and c != LF) {
                expect_lf = false;
                dp = WRITE_CHR(dp, LF);
                ++num_chars;
            }
            if (c == CR) {
                expect_lf = true;
                continue;
            }
        }

        // !!! "check for surrogate pair" ??

        if (c > 127)
            ascii = false;

        dp = WRITE_CHR(dp, c);
        ++num_chars;
    }

    // !!! The ascii flag should be preserved in the series node for faster
    // operations on UTF-8
    //
    UNUSED(ascii);

    TERM_STR_LEN_SIZE(s, num_chars, dp - STR_HEAD(s));
    return s;
}


//
//  export identify-text?: native [
//
//  {Codec for identifying BINARY! data for a .TXT file}
//
//      return: [logic!]
//      data [binary!]
//  ]
//
REBNATIVE(identify_text_q)
{
    UTF_INCLUDE_PARAMS_OF_IDENTIFY_TEXT_Q;

    UNUSED(ARG(data)); // see notes on decode-text

    return Init_True(OUT);
}


//
//  export decode-text: native [
//
//  {Codec for decoding BINARY! data for a .TXT file}
//
//      return: [text!]
//      data [binary!]
//  ]
//
REBNATIVE(decode_text)
{
    UTF_INCLUDE_PARAMS_OF_DECODE_TEXT;

    // !!! The original code for R3-Alpha would simply alias the incoming
    // binary as a string.  This is essentially a Latin1 interpretation.
    // For the moment that behavior is preserved, but what is *not* preserved
    // is the idea of reusing the BINARY!--a copy is made.
    //
    // A more "intelligent" codec would do some kind of detection here, to
    // figure out what format the text file was in.  While Ren-C's commitment
    // is to UTF-8 for source code, a .TXT file is a different beast, so
    // having wider format support might be a good thing.

    REBSIZ size;
    const REBYTE *data = VAL_BINARY_SIZE_AT(&size, ARG(data));
    return rebSizedText(cs_cast(data), size);
}


//
//  export encode-text: native [
//
//  {Codec for encoding a .TXT file}
//
//      return: [binary!]
//      string [text!]
//  ]
//
REBNATIVE(encode_text)
{
    UTF_INCLUDE_PARAMS_OF_ENCODE_TEXT;

    UNUSED(PAR(string));

    fail (".txt codec not currently implemented (what should it do?)");
}


static REBSER *Encode_Utf16(
    REBCHR(const*) data,
    REBLEN len,
    bool little_endian
){
    REBCHR(const*) cp = data;

    REBBIN *bin = Make_Binary(sizeof(uint16_t) * len);
    uint16_t* up = cast(uint16_t*, BIN_HEAD(bin));

    REBLEN i = 0;
    for (i = 0; i < len; ++i) {
        REBUNI c;
        cp = NEXT_CHR(&c, cp);

        // !!! TBD: handle large codepoints bigger than 0xffff, and encode
        // as UTF16.  (REBUNI is only 16 bits at time of writing)

      #if defined(ENDIAN_LITTLE)
        if (little_endian)
            up[i] = c;
        else
            up[i] = ((c & 0xff) << 8) | ((c & 0xff00) >> 8);
      #elif defined(ENDIAN_BIG)
        if (little_endian)
            up[i] = ((c & 0xff) << 8) | ((c & 0xff00) >> 8);
        else
            up[i] = c;
      #else
        #error "Unsupported CPU endian"
      #endif
    }

    up[i] = '\0'; // needs two bytes worth of NULL, not just one.

    SET_SERIES_LEN(bin, len * sizeof(uint16_t));
    return bin;
}


//
//  export identify-utf16le?: native [
//
//  {Codec for identifying BINARY! data for a little-endian UTF16 file}
//
//      return: [logic!]
//      data [binary!]
//  ]
//
REBNATIVE(identify_utf16le_q)
{
    UTF_INCLUDE_PARAMS_OF_IDENTIFY_UTF16LE_Q;

    // R3-Alpha just said it matched if extension matched.  It could look for
    // a byte order mark by default, but perhaps that's the job of the more
    // general ".txt" codec...because if you ask specifically to decode a
    // stream as UTF-16-LE, then you may be willing to tolerate no BOM.
    //
    UNUSED(ARG(data));

    return Init_True(OUT);
}


//
//  export decode-utf16le: native [
//
//  {Codec for decoding BINARY! data for a little-endian UTF16 file}
//
//      return: [text!]
//      data [binary!]
//  ]
//
REBNATIVE(decode_utf16le)
{
    UTF_INCLUDE_PARAMS_OF_DECODE_UTF16LE;

    REBSIZ size;
    const REBYTE *data = VAL_BINARY_SIZE_AT(&size, ARG(data));

    const bool little_endian = true;
    Init_Text(OUT, Decode_UTF16(data, size, little_endian, false));

    // Drop byte-order marker, if present
    //
    rebElide("if #\"^(FEFF)\" = first", OUT, "[",
        "take", OUT,
    "]");

    return OUT;
}


//
//  export encode-utf16le: native [
//
//  {Codec for encoding a little-endian UTF16 file}
//
//      return: [binary!]
//      text [text!]
//  ]
//
REBNATIVE(encode_utf16le)
{
    UTF_INCLUDE_PARAMS_OF_ENCODE_UTF16LE;

    REBLEN len;
    REBCHR(const*) utf8 = VAL_UTF8_LEN_SIZE_AT(&len, nullptr, ARG(text));

    const bool little_endian = true;
    Init_Binary(OUT, Encode_Utf16(utf8, len, little_endian));

    // !!! Should probably by default add a byte order mark, but given this
    // is weird "userspace" encoding it should be an option to the codec.

    return OUT;
}



//
//  export identify-utf16be?: native [
//
//  {Codec for identifying BINARY! data for a big-endian UTF16 file}
//
//      return: [logic!]
//      data [binary!]
//  ]
//
REBNATIVE(identify_utf16be_q)
{
    UTF_INCLUDE_PARAMS_OF_IDENTIFY_UTF16BE_Q;

    // R3-Alpha just said it matched if extension matched.  It could look for
    // a byte order mark by default, but perhaps that's the job of the more
    // general ".txt" codec...because if you ask specifically to decode a
    // stream as UTF-16-BE, then you may be willing to tolerate no BOM.
    //
    UNUSED(ARG(data));

    return Init_True(OUT);
}


//
//  export decode-utf16be: native [
//
//  {Codec for decoding BINARY! data for a big-endian UTF16 file}
//
//      return: [text!]
//      data [binary!]
//  ]
//
REBNATIVE(decode_utf16be)
{
    UTF_INCLUDE_PARAMS_OF_DECODE_UTF16BE;

    REBSIZ size;
    const REBYTE *data = VAL_BINARY_SIZE_AT(&size, ARG(data));

    const bool little_endian = false;
    Init_Text(OUT, Decode_UTF16(data, size, little_endian, false));

    // Drop byte-order marker, if present
    //
    rebElide(
        "if #\"^(FEFF)\" = first", OUT, "[take", OUT, "]"
    );

    return OUT;
}


//
//  export encode-utf16be: native [
//
//  {Codec for encoding a big-endian UTF16 file}
//
//      return: [binary!]
//      text [text!]
//  ]
//
REBNATIVE(encode_utf16be)
{
    UTF_INCLUDE_PARAMS_OF_ENCODE_UTF16BE;

    REBLEN len;
    REBCHR(const*) utf8 = VAL_UTF8_LEN_SIZE_AT(&len, nullptr, ARG(text));

    const bool little_endian = false;
    Init_Binary(OUT, Encode_Utf16(utf8, len, little_endian));

    // !!! Should probably by default add a byte order mark, but given this
    // is weird "userspace" encoding it should be an option to the codec.

    return OUT;
}
