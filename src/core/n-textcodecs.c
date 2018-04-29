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
REBINT What_UTF(const REBYTE *bp, REBCNT len)
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
    REBUNI *dst,
    const REBYTE *src,
    REBCNT len,
    REBOOL little_endian,
    REBOOL crlf_to_lf
){
    REBOOL expect_lf = FALSE;
    REBOOL ascii = TRUE;
    uint32_t ch;
    REBUNI *start = dst;

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
                expect_lf = FALSE;
                *dst++ = LF;
            }
            if (ch == CR) {
                expect_lf = TRUE;
                continue;
            }
        }

        // !!! "check for surrogate pair" ??

        if (ch > 127)
            ascii = FALSE;

        *dst++ = cast(REBUNI, ch);
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
REBNATIVE(identify_text_q)
{
    INCLUDE_PARAMS_OF_IDENTIFY_TEXT_Q;

    UNUSED(ARG(data)); // see notes on decode-text

    return R_TRUE;
}


//
//  decode-text: native [
//
//  {Codec for decoding BINARY! data for a .TXT file}
//
//      return: [string!]
//      data [binary!]
//  ]
//
REBNATIVE(decode_text)
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

    Init_String(D_OUT, Make_String_UTF8(cs_cast(VAL_BIN_AT(ARG(data)))));
    return R_OUT;
}


//
//  encode-text: native [
//
//  {Codec for encoding a .TXT file}
//
//      return: [binary!]
//      string [string!]
//  ]
//
REBNATIVE(encode_text)
{
    INCLUDE_PARAMS_OF_ENCODE_TEXT;

    if (not VAL_BYTE_SIZE(ARG(string))) {
        //
        // For the moment, only write out strings to .txt if they are Latin1.
        // (Other support was unimplemented in R3-Alpha, and would just wind
        // up writing garbage.)
        //
        fail ("Can only write out strings to .txt if they are Latin1.");
    }

    Init_Binary(D_OUT, Copy_Sequence_At_Position(ARG(string)));
    return R_OUT;
}


static void Encode_Utf16_Core(
    REBVAL *out,
    REBCHR(const *) data,
    REBCNT len,
    REBOOL little_endian
){
    REBCHR(const *) cp = data;

    REBSER *bin = Make_Binary(sizeof(uint16_t) * len);
    uint16_t* up = cast(uint16_t*, BIN_HEAD(bin));

    REBCNT i = 0;
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
    Init_Binary(out, bin);
}


static void Decode_Utf16_Core(
    REBVAL *out,
    const REBYTE *data,
    REBCNT len,
    REBOOL little_endian
){
    REBSER *ser = Make_Unicode(len); // 2x too big (?)

    REBINT size = Decode_UTF16_Negative_If_ASCII(
        UNI_HEAD(ser), data, len, little_endian, FALSE
    );
    if (size < 0) // ASCII
        size = -size;
    TERM_UNI_LEN(ser, size);

    Init_String(out, ser);
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
REBNATIVE(identify_utf16le_q)
{
    INCLUDE_PARAMS_OF_IDENTIFY_UTF16LE_Q;

    // R3-Alpha just said it matched if extension matched.  It could look for
    // a byte order mark by default, but perhaps that's the job of the more
    // general ".txt" codec...because if you ask specifically to decode a
    // stream as UTF-16-LE, then you may be willing to tolerate no BOM.
    //
    UNUSED(ARG(data));

    return R_TRUE;
}


//
//  decode-utf16le: native [
//
//  {Codec for decoding BINARY! data for a little-endian UTF16 file}
//
//      return: [string!]
//      data [binary!]
//  ]
//
REBNATIVE(decode_utf16le)
{
    INCLUDE_PARAMS_OF_DECODE_UTF16LE;

    REBYTE *data = VAL_BIN_AT(ARG(data));
    REBCNT len = VAL_LEN_AT(ARG(data));

    const REBOOL little_endian = TRUE;

    Decode_Utf16_Core(D_OUT, data, len, little_endian);

    // Drop byte-order marker, if present
    //
    if (
        VAL_LEN_AT(D_OUT) > 0
        && GET_ANY_CHAR(VAL_SERIES(D_OUT), VAL_INDEX(D_OUT)) == 0xFEFF
    ){
        Remove_Series(VAL_SERIES(D_OUT), VAL_INDEX(D_OUT), 1);
    }

    return R_OUT;
}


//
//  encode-utf16le: native [
//
//  {Codec for encoding a little-endian UTF16 file}
//
//      return: [binary!]
//      string [string!]
//  ]
//
REBNATIVE(encode_utf16le)
{
    INCLUDE_PARAMS_OF_ENCODE_UTF16LE;

    // !!! Should probably by default add a byte order mark, but given this
    // is weird "userspace" encoding it should be an option to the codec.

    const REBOOL little_endian = TRUE;
    Encode_Utf16_Core(
        D_OUT,
        VAL_UNI_AT(ARG(string)),
        VAL_LEN_AT(ARG(string)),
        little_endian
    );
    return R_OUT;
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
REBNATIVE(identify_utf16be_q)
{
    INCLUDE_PARAMS_OF_IDENTIFY_UTF16BE_Q;

    // R3-Alpha just said it matched if extension matched.  It could look for
    // a byte order mark by default, but perhaps that's the job of the more
    // general ".txt" codec...because if you ask specifically to decode a
    // stream as UTF-16-BE, then you may be willing to tolerate no BOM.
    //
    UNUSED(ARG(data));

    return R_TRUE;
}


//
//  decode-utf16be: native [
//
//  {Codec for decoding BINARY! data for a big-endian UTF16 file}
//
//      return: [string!]
//      data [binary!]
//  ]
//
REBNATIVE(decode_utf16be)
{
    INCLUDE_PARAMS_OF_DECODE_UTF16BE;

    REBYTE *data = VAL_BIN_AT(ARG(data));
    REBCNT len = VAL_LEN_AT(ARG(data));

    const REBOOL little_endian = FALSE;

    Decode_Utf16_Core(D_OUT, data, len, little_endian);

    // Drop byte-order marker, if present
    //
    if (
        VAL_LEN_AT(D_OUT) > 0
        && GET_ANY_CHAR(VAL_SERIES(D_OUT), VAL_INDEX(D_OUT)) == 0xFEFF
    ){
        Remove_Series(VAL_SERIES(D_OUT), VAL_INDEX(D_OUT), 1);
    }

    return R_OUT;
}


//
//  encode-utf16be: native [
//
//  {Codec for encoding a big-endian UTF16 file}
//
//      return: [binary!]
//      string [string!]
//  ]
//
REBNATIVE(encode_utf16be)
{
    INCLUDE_PARAMS_OF_ENCODE_UTF16BE;

    const REBOOL little_endian = FALSE;

    // !!! Should probably by default add a byte order mark, but given this
    // is weird "userspace" encoding it should be an option to the codec.

    Encode_Utf16_Core(
        D_OUT,
        VAL_UNI_AT(ARG(string)),
        VAL_LEN_AT(ARG(string)),
        little_endian
    );
    return R_OUT;
}
