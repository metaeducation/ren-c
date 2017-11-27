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
// Rebol values.  Under Ren-C these are done as plain FUNCTION!s, which can
// be coded in either C as natives or Rebol.
//
// A few incomplete text codecs were included in R3-Alpha, and have been
// kept around for testing.  They were converted here into groups of native
// functions, but should be further moved into an extension so they can be
// optional in the build.
//

#include "sys-core.h"


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

    Init_String(D_OUT, Make_UTF8_May_Fail(cs_cast(VAL_BIN_AT(ARG(data)))));
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

    if (NOT(VAL_BYTE_SIZE(ARG(string)))) {
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
    REBUNI *data,
    REBCNT len,
    REBOOL little_endian
){
    REBSER *bin = Make_Binary(sizeof(u16) * len);
    u16* up = cast(u16*, BIN_HEAD(bin));

    // Currently only supports UCS2, which is close to UTF16 :-/
#ifdef ENDIAN_LITTLE
    if (little_endian) {
        memcpy(up, data, len * sizeof(u16));
    } else {
        REBCNT i = 0;
        for (i = 0; i < len; i ++) {
            REBUNI c = data[i];
            up[i] = ((c & 0xff) << 8) | ((c & 0xff00) >> 8);
        }
    }
#elif defined (ENDIAN_BIG)
    if (little_endian) {
        REBCNT i = 0;
        for (i = 0; i < len; i ++) {
            REBUNI c = data[i];
            up[i] = ((c & 0xff) << 8) | ((c & 0xff00) >> 8);
        }
    } else {
        memcpy(up, data, len * sizeof(u16));
    }
#else
    #error "Unsupported CPU endian"
#endif

    TERM_BIN_LEN(bin, len * sizeof(u16));
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
    if (VAL_LEN_AT(D_OUT) > 0 && *VAL_UNI_AT(D_OUT) == 0xFEFF)
        Remove_Series(VAL_SERIES(D_OUT), VAL_INDEX(D_OUT), 1);

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
    if (VAL_LEN_AT(D_OUT) > 0 && *VAL_UNI_AT(D_OUT) == 0xFEFF)
        Remove_Series(VAL_SERIES(D_OUT), VAL_INDEX(D_OUT), 1);

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
