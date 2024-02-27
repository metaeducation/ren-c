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

#include "tmp-mod-utf.h"


//
//  Detect_UTF: C
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
REBINT Detect_UTF(const Byte* bp, Size size)
{
    if (size >= 3 && bp[0] == 0xef && bp[1] == 0xbb && bp[2] == 0xbf)
        return 8; // UTF8 (endian agnostic)

    if (size >= 2) {
        if (bp[0] == 0xfe && bp[1] == 0xff)
            return 16; // UTF16 big endian

        if (bp[0] == 0xff && bp[1] == 0xfe) {
            if (size >= 4 && bp[2] == 0 && bp[3] == 0)
                return -32; // UTF32 little endian
            return -16; // UTF16 little endian
        }

        if (
            size >= 4
            && bp[0] == 0 && bp[1] == 0 && bp[2] == 0xfe && bp[3] == 0xff
        ){
            return 32; // UTF32 big endian
        }
    }

    return 0; // unknown
}


//
//  Decode_UCS2: C
//
// No terminator is added.
//
// 1. Currently there is no support for "surrogate pairs", so only characters
//    which can be represented in a single 2-byte are covered (UCS2), not
//    variable-size encoded 2-byte or 4-byte (UTF16).
//
// 2. This routine doesn't contain resizing logic, so it makes the conservative
//    allocation that the string would require 4 encoded bytes for every
//    2-byte UTF16 encoded char.
//
// 3. All-ASCII optimization flag on strings is a work-in-progress.
//
static String* Decode_UCS2(  // [1]
    const Byte* src,
    Size size,  // byte length of source (not number of codepoints)
    bool little_endian,
    bool crlf_to_lf
){
    String* s = Make_String(size * 2);  // conservative over-alloc [2]

    bool expect_lf = false;
    bool ascii = true;
    Codepoint c;

    Length num_chars = 0;

    Utf8(*) dest = String_Head(s);

    for (; size > 0; --size, ++src) {
        c = *src;
        if (not little_endian)
            c <<= 8;
        if (--size <= 0)
            break;

        ++src;

        c |= little_endian ? (cast(Codepoint, *src) << 8) : *src;

        // !!! "check for surrogate pair" [2]

        if (crlf_to_lf) {  // Skip CR, but add LF (even if missing)
            if (expect_lf and c != LF) {
                expect_lf = false;
                dest = Write_Codepoint(dest, LF);
                ++num_chars;
            }
            if (c == CR) {
                expect_lf = true;
                continue;
            }
        }

        if (c > 127)
            ascii = false;

        dest = Write_Codepoint(dest, c);
        ++num_chars;
    }

    UNUSED(ascii);  // [3]

    Term_String_Len_Size(s, num_chars, dest - String_Head(s));
    return s;
}


//
//  export identify-text?: native [
//
//  "Codec for identifying BINARY! data for a .TXT file"
//
//      return: [logic?]
//      data [binary!]
//  ]
//
DECLARE_NATIVE(identify_text_q)
{
    INCLUDE_PARAMS_OF_IDENTIFY_TEXT_Q;

    UNUSED(ARG(data)); // see notes on decode-text

    return Init_True(OUT);
}


//
//  export decode-text: native [
//
//  "Codec for decoding BINARY! data for a .TXT file"
//
//      return: [text!]
//      data [binary!]
//  ]
//
DECLARE_NATIVE(decode_text)
{
    INCLUDE_PARAMS_OF_DECODE_TEXT;

    // !!! The original code for R3-Alpha would simply alias the incoming
    // binary as a string.  This is essentially a Latin1 interpretation.
    // For the moment that behavior is preserved, but what is *not* preserved
    // is the idea of reusing the BINARY!--a copy is made.
    //
    // A more "intelligent" codec would do some kind of detection here, to
    // figure out what format the text file was in.  While Ren-C's commitment
    // is to UTF-8 for source code, a .TXT file is a different beast, so
    // having wider format support might be a good thing.

    Size size;
    const Byte* data = Cell_Binary_Size_At(&size, ARG(data));
    return rebSizedText(cs_cast(data), size);
}


//
//  export encode-text: native [
//
//  "Codec for encoding a .TXT file"
//
//      return: [binary!]
//      string [text!]
//  ]
//
DECLARE_NATIVE(encode_text)
{
    INCLUDE_PARAMS_OF_ENCODE_TEXT;

    UNUSED(PARAM(string));

    fail (".txt codec not currently implemented (what should it do?)");
}


// 1. TBD: handle large codepoints bigger than 0xffff, and encode as UTF16
//    instead of just UCS2.
//
static Binary* Encode_UCS2(  // [1]
    Utf8(const*) utf8,
    Length len,
    bool little_endian
){
    Binary* bin = Make_Binary(sizeof(uint16_t) * len);
    uint16_t* ucs2 = cast(uint16_t*, Binary_Head(bin));

    Count n = 0;
    for (n = 0; n < len; ++n) {
        Codepoint c;
        utf8 = Utf8_Next(&c, utf8);

      #if defined(ENDIAN_LITTLE)
        if (little_endian)
            ucs2[n] = c;
        else
            ucs2[n] = ((c & 0xff) << 8) | ((c & 0xff00) >> 8);
      #elif defined(ENDIAN_BIG)
        if (little_endian)
            ucs2[n] = ((c & 0xff) << 8) | ((c & 0xff00) >> 8);
        else
            ucs2[n] = c;
      #else
        #error "Unsupported CPU endian"
      #endif
    }

    ucs2[n] = '\0';  // needs two bytes worth of NULL, not just one.

    Set_Series_Len(bin, len * sizeof(uint16_t));
    return bin;
}


//
//  export identify-utf16le?: native [
//
//  "Codec for identifying BINARY! data for a little-endian UTF16 file"
//
//      return: [logic?]
//      data [binary!]
//  ]
//
DECLARE_NATIVE(identify_utf16le_q)
{
    INCLUDE_PARAMS_OF_IDENTIFY_UTF16LE_Q;

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
//  "Codec for decoding BINARY! data for a little-endian UTF16 file"
//
//      return: [text!]
//      data [binary!]
//  ]
//
DECLARE_NATIVE(decode_utf16le)
{
    INCLUDE_PARAMS_OF_DECODE_UTF16LE;

    Size size;
    const Byte* data = Cell_Binary_Size_At(&size, ARG(data));

    const bool little_endian = true;
    Init_Text(OUT, Decode_UCS2(data, size, little_endian, false));

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
//  "Codec for encoding a little-endian UTF16 file"
//
//      return: [binary!]
//      text [text!]
//  ]
//
DECLARE_NATIVE(encode_utf16le)
{
    INCLUDE_PARAMS_OF_ENCODE_UTF16LE;

    Length len;
    Utf8(const*) utf8 = Cell_Utf8_Len_Size_At(&len, nullptr, ARG(text));

    const bool little_endian = true;
    Init_Binary(OUT, Encode_UCS2(utf8, len, little_endian));

    // !!! Should probably by default add a byte order mark, but given this
    // is weird "userspace" encoding it should be an option to the codec.

    return OUT;
}



//
//  export identify-utf16be?: native [
//
//  "Codec for identifying BINARY! data for a big-endian UTF16 file"
//
//      return: [logic?]
//      data [binary!]
//  ]
//
DECLARE_NATIVE(identify_utf16be_q)
{
    INCLUDE_PARAMS_OF_IDENTIFY_UTF16BE_Q;

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
//  "Codec for decoding BINARY! data for a big-endian UTF16 file"
//
//      return: [text!]
//      data [binary!]
//  ]
//
DECLARE_NATIVE(decode_utf16be)
{
    INCLUDE_PARAMS_OF_DECODE_UTF16BE;

    Size size;
    const Byte* data = Cell_Binary_Size_At(&size, ARG(data));

    const bool little_endian = false;
    Init_Text(OUT, Decode_UCS2(data, size, little_endian, false));

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
//  "Codec for encoding a big-endian UTF16 file"
//
//      return: [binary!]
//      text [text!]
//  ]
//
DECLARE_NATIVE(encode_utf16be)
{
    INCLUDE_PARAMS_OF_ENCODE_UTF16BE;

    Length len;
    Utf8(const*) utf8 = Cell_Utf8_Len_Size_At(&len, nullptr, ARG(text));

    const bool little_endian = false;
    Init_Binary(OUT, Encode_UCS2(utf8, len, little_endian));

    // !!! Should probably by default add a byte order mark, but given this
    // is weird "userspace" encoding it should be an option to the codec.

    return OUT;
}
