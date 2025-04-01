//
//  File: %n-textcodec.c
//  Summary: "Native text codecs"
//  Section: natives
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
// R3-Alpha had an incomplete model for doing codecs, that required C coding
// to implement...even though the input and output types to DO-CODEC were
// Rebol values.  Under Ren-C these are done as plain ACTION!s, which can
// be coded in either C as natives or Rebol.
//
// A few incomplete text codecs were included in R3-Alpha, and have been
// kept around for testing.  They were converted here into groups of native
// functions, but should be further moved into an extension so they can be
// optional in the build.
//

#include "sys-core.h"


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
REBINT What_UTF(const Byte *bp, REBLEN len)
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
// dst: the desination array, must always be large enough!
// src: source binary data
// len: byte-length of source (not number of chars)
// little_endian: little endian encoded
// crlf_to_lf: convert CRLF/CR to LF
//
// Returns length in chars (negative if all chars are ASCII).
// No terminator is added.
//
int Decode_UTF16_Negative_If_ASCII(
    Ucs2Unit* dst,
    const Byte *src,
    REBLEN len,
    bool little_endian,
    bool crlf_to_lf
){
    bool expect_lf = false;
    bool ascii = true;
    uint32_t ch;
    Ucs2Unit* start = dst;

    for (; len > 0; len--, src++) {
        //
        // Combine bytes in big or little endian format
        //
        ch = *src;
        if (!little_endian) ch <<= 8;
        if (--len <= 0) break;
        src++;
        ch |= little_endian ? (cast(uint32_t, *src) << 8) : *src;

        if (crlf_to_lf) {
            //
            // Skip CR, but add LF (even if missing)
            //
            if (expect_lf && ch != LF) {
                expect_lf = false;
                *dst++ = LF;
            }
            if (ch == CR) {
                expect_lf = true;
                continue;
            }
        }

        // !!! "check for surrogate pair" ??

        if (ch > 127)
            ascii = false;

        *dst++ = cast(Ucs2Unit, ch);
    }

    return ascii ? -(dst - start) : (dst - start);
}


//
//  identify-text?: native [
//
//  {Codec for identifying BINARY! data for a .TXT file}
//
//      return: [logic!]
//      data [binary!]
//  ]
//
DECLARE_NATIVE(IDENTIFY_TEXT_Q)
{
    INCLUDE_PARAMS_OF_IDENTIFY_TEXT_Q;

    UNUSED(ARG(DATA)); // see notes on decode-text

    return Init_True(OUT);
}


//
//  decode-text: native [
//
//  {Codec for decoding BINARY! data for a .TXT file}
//
//      return: [text!]
//      data [binary!]
//  ]
//
DECLARE_NATIVE(DECODE_TEXT)
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

    Init_Text(OUT, Make_String_UTF8(cs_cast(Cell_Blob_At(ARG(DATA)))));
    return OUT;
}


//
//  encode-text: native [
//
//  {Codec for encoding a .TXT file}
//
//      return: [binary!]
//      string [text!]
//  ]
//
DECLARE_NATIVE(ENCODE_TEXT)
{
    INCLUDE_PARAMS_OF_ENCODE_TEXT;

    if (not VAL_BYTE_SIZE(ARG(STRING))) {
        //
        // For the moment, only write out strings to .txt if they are Latin1.
        // (Other support was unimplemented in R3-Alpha, and would just wind
        // up writing garbage.)
        //
        fail ("Can only write out strings to .txt if they are Latin1.");
    }

    return Init_Blob(OUT, Copy_Sequence_At_Position(ARG(STRING)));
}


static void Encode_Utf16_Core(
    Value* out,
    Ucs2(const*) data,
    REBLEN len,
    bool little_endian
){
    Ucs2(const*) cp = data;

    Binary* bin = Make_Binary(sizeof(uint16_t) * len);
    uint16_t* up = cast(uint16_t*, Binary_Head(bin));

    REBLEN i = 0;
    for (i = 0; i < len; ++i) {
        Ucs2Unit c;
        cp = Ucs2_Next(&c, cp);

        // !!! TBD: handle large codepoints bigger than 0xffff, and encode
        // as UTF16.  (Ucs2Unit is only 16 bits at time of writing)

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

    up[i] = '\0'; // needs two bytes worth of terminator, not just one.

    Set_Flex_Len(bin, len * sizeof(uint16_t));
    Init_Blob(out, bin);
}


static void Decode_Utf16_Core(
    Value* out,
    const Byte *data,
    REBLEN len,
    bool little_endian
){
    String* flex = Make_String(len); // 2x too big (?)

    REBINT size = Decode_UTF16_Negative_If_ASCII(
        String_Head(flex), data, len, little_endian, false
    );
    if (size < 0) // ASCII
        size = -size;
    Term_String_Len(flex, size);

    Init_Text(out, flex);
}


//
//  identify-utf16le?: native [
//
//  {Codec for identifying BINARY! data for a little-endian UTF16 file}
//
//      return: [logic!]
//      data [binary!]
//  ]
//
DECLARE_NATIVE(IDENTIFY_UTF16LE_Q)
{
    INCLUDE_PARAMS_OF_IDENTIFY_UTF16LE_Q;

    // R3-Alpha just said it matched if extension matched.  It could look for
    // a byte order mark by default, but perhaps that's the job of the more
    // general ".txt" codec...because if you ask specifically to decode a
    // stream as UTF-16-LE, then you may be willing to tolerate no BOM.
    //
    UNUSED(ARG(DATA));

    return Init_True(OUT);
}


//
//  decode-utf16le: native [
//
//  {Codec for decoding BINARY! data for a little-endian UTF16 file}
//
//      return: [text!]
//      data [binary!]
//  ]
//
DECLARE_NATIVE(DECODE_UTF16LE)
{
    INCLUDE_PARAMS_OF_DECODE_UTF16LE;

    Byte *data = Cell_Blob_At(ARG(DATA));
    REBLEN len = Cell_Series_Len_At(ARG(DATA));

    const bool little_endian = true;

    Decode_Utf16_Core(OUT, data, len, little_endian);

    // Drop byte-order marker, if present
    //
    if (
        Cell_Series_Len_At(OUT) > 0
        && GET_ANY_CHAR(Cell_Flex(OUT), VAL_INDEX(OUT)) == 0xFEFF
    ){
        Remove_Flex(Cell_Flex(OUT), VAL_INDEX(OUT), 1);
    }

    return OUT;
}


//
//  encode-utf16le: native [
//
//  {Codec for encoding a little-endian UTF16 file}
//
//      return: [binary!]
//      text [text!]
//  ]
//
DECLARE_NATIVE(ENCODE_UTF16LE)
{
    INCLUDE_PARAMS_OF_ENCODE_UTF16LE;

    // !!! Should probably by default add a byte order mark, but given this
    // is weird "userspace" encoding it should be an option to the codec.

    const bool little_endian = true;
    Encode_Utf16_Core(
        OUT,
        Cell_String_At(ARG(TEXT)),
        Cell_Series_Len_At(ARG(TEXT)),
        little_endian
    );
    return OUT;
}



//
//  identify-utf16be?: native [
//
//  {Codec for identifying BINARY! data for a big-endian UTF16 file}
//
//      return: [logic!]
//      data [binary!]
//  ]
//
DECLARE_NATIVE(IDENTIFY_UTF16BE_Q)
{
    INCLUDE_PARAMS_OF_IDENTIFY_UTF16BE_Q;

    // R3-Alpha just said it matched if extension matched.  It could look for
    // a byte order mark by default, but perhaps that's the job of the more
    // general ".txt" codec...because if you ask specifically to decode a
    // stream as UTF-16-BE, then you may be willing to tolerate no BOM.
    //
    UNUSED(ARG(DATA));

    return Init_True(OUT);
}


//
//  decode-utf16be: native [
//
//  {Codec for decoding BINARY! data for a big-endian UTF16 file}
//
//      return: [text!]
//      data [binary!]
//  ]
//
DECLARE_NATIVE(DECODE_UTF16BE)
{
    INCLUDE_PARAMS_OF_DECODE_UTF16BE;

    Byte *data = Cell_Blob_At(ARG(DATA));
    REBLEN len = Cell_Series_Len_At(ARG(DATA));

    const bool little_endian = false;

    Decode_Utf16_Core(OUT, data, len, little_endian);

    // Drop byte-order marker, if present
    //
    if (
        Cell_Series_Len_At(OUT) > 0
        && GET_ANY_CHAR(Cell_Flex(OUT), VAL_INDEX(OUT)) == 0xFEFF
    ){
        Remove_Flex(Cell_Flex(OUT), VAL_INDEX(OUT), 1);
    }

    return OUT;
}


//
//  encode-utf16be: native [
//
//  {Codec for encoding a big-endian UTF16 file}
//
//      return: [binary!]
//      text [text!]
//  ]
//
DECLARE_NATIVE(ENCODE_UTF16BE)
{
    INCLUDE_PARAMS_OF_ENCODE_UTF16BE;

    const bool little_endian = false;

    // !!! Should probably by default add a byte order mark, but given this
    // is weird "userspace" encoding it should be an option to the codec.

    Encode_Utf16_Core(
        OUT,
        Cell_String_At(ARG(TEXT)),
        Cell_Series_Len_At(ARG(TEXT)),
        little_endian
    );
    return OUT;
}
