//
//  File: %f-enbase.c
//  Summary: "base representation conversions"
//  Section: functional
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
// Base-64 binary decoder table.
//
static const REBYTE Debase64[128] =
{
    #define BIN_ERROR   (REBYTE)0x80
    #define BIN_SPACE   (REBYTE)0x40
    #define BIN_VALUE   (REBYTE)0x3f
    #define IS_BIN_SPACE(c) LOGICAL(Debase64[c] & BIN_SPACE)

    /* Control Chars */
    BIN_ERROR,BIN_ERROR,BIN_ERROR,BIN_ERROR,    /* 80 */
    BIN_ERROR,BIN_ERROR,BIN_ERROR,BIN_ERROR,
    BIN_SPACE,BIN_SPACE,BIN_SPACE,BIN_ERROR,
    BIN_SPACE,BIN_SPACE,BIN_ERROR,BIN_ERROR,
    BIN_ERROR,BIN_ERROR,BIN_ERROR,BIN_ERROR,
    BIN_ERROR,BIN_ERROR,BIN_ERROR,BIN_ERROR,
    BIN_ERROR,BIN_ERROR,BIN_ERROR,BIN_ERROR,
    BIN_ERROR,BIN_ERROR,BIN_ERROR,BIN_ERROR,

    /* 20     */    BIN_SPACE,
    /* 21 !   */    BIN_ERROR,
    /* 22 "   */    BIN_ERROR,
    /* 23 #   */    BIN_ERROR,
    /* 24 $   */    BIN_ERROR,
    /* 25 %   */    BIN_ERROR,
    /* 26 &   */    BIN_ERROR,
    /* 27 '   */    BIN_SPACE,
    /* 28 (   */    BIN_ERROR,
    /* 29 )   */    BIN_ERROR,
    /* 2A *   */    BIN_ERROR,
    /* 2B +   */    62,
    /* 2C ,   */    BIN_ERROR,
    /* 2D -   */    BIN_ERROR,
    /* 2E .   */    BIN_ERROR,
    /* 2F /   */    63,

    /* 30 0   */    52,
    /* 31 1   */    53,
    /* 32 2   */    54,
    /* 33 3   */    55,
    /* 34 4   */    56,
    /* 35 5   */    57,
    /* 36 6   */    58,
    /* 37 7   */    59,
    /* 38 8   */    60,
    /* 39 9   */    61,
    /* 3A :   */    BIN_ERROR,
    /* 3B ;   */    BIN_ERROR,
    /* 3C <   */    BIN_ERROR,
    /* 3D =   */    0,      // pad char
    /* 3E >   */    BIN_ERROR,
    /* 3F ?   */    BIN_ERROR,

    /* 40 @   */    BIN_ERROR,
    /* 41 A   */    0,
    /* 42 B   */    1,
    /* 43 C   */    2,
    /* 44 D   */    3,
    /* 45 E   */    4,
    /* 46 F   */    5,
    /* 47 G   */    6,
    /* 48 H   */    7,
    /* 49 I   */    8,
    /* 4A J   */    9,
    /* 4B K   */    10,
    /* 4C L   */    11,
    /* 4D M   */    12,
    /* 4E N   */    13,
    /* 4F O   */    14,

    /* 50 P   */    15,
    /* 51 Q   */    16,
    /* 52 R   */    17,
    /* 53 S   */    18,
    /* 54 T   */    19,
    /* 55 U   */    20,
    /* 56 V   */    21,
    /* 57 W   */    22,
    /* 58 X   */    23,
    /* 59 Y   */    24,
    /* 5A Z   */    25,
    /* 5B [   */    BIN_ERROR,
    /* 5C \   */    BIN_ERROR,
    /* 5D ]   */    BIN_ERROR,
    /* 5E ^   */    BIN_ERROR,
    /* 5F _   */    BIN_ERROR,

    /* 60 `   */    BIN_ERROR,
    /* 61 a   */    26,
    /* 62 b   */    27,
    /* 63 c   */    28,
    /* 64 d   */    29,
    /* 65 e   */    30,
    /* 66 f   */    31,
    /* 67 g   */    32,
    /* 68 h   */    33,
    /* 69 i   */    34,
    /* 6A j   */    35,
    /* 6B k   */    36,
    /* 6C l   */    37,
    /* 6D m   */    38,
    /* 6E n   */    39,
    /* 6F o   */    40,

    /* 70 p   */    41,
    /* 71 q   */    42,
    /* 72 r   */    43,
    /* 73 s   */    44,
    /* 74 t   */    45,
    /* 75 u   */    46,
    /* 76 v   */    47,
    /* 77 w   */    48,
    /* 78 x   */    49,
    /* 79 y   */    50,
    /* 7A z   */    51,
    /* 7B {   */    BIN_ERROR,
    /* 7C |   */    BIN_ERROR,
    /* 7D }   */    BIN_ERROR,
    /* 7E ~   */    BIN_ERROR,
    /* 7F DEL */    BIN_ERROR,
};


// Base-64 binary encoder table.
//
// NOTE: Entered one-character-at-a-time in array initialization
// format to avoid the length of 65 which would be needed if
// a string literal were used.  This helps memory tools trap
// errant accesses to Enbase64[64] if there's an algorithm bug.
//
static const REBYTE Enbase64[64] =
{
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
    'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
    'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/'
};


//
//  Decode_Base2: C
//
static REBSER *Decode_Base2(const REBYTE **src, REBCNT len, REBYTE delim)
{
    REBYTE *bp;
    const REBYTE *cp;
    REBCNT count = 0;
    REBCNT accum = 0;
    REBYTE lex;
    REBSER *ser;

    ser = Make_Binary(len >> 3);
    bp = BIN_HEAD(ser);
    cp = *src;

    for (; len > 0; cp++, len--) {

        if (delim && *cp == delim) break;

        lex = Lex_Map[*cp];

        if (lex >= LEX_NUMBER) {

            if (*cp == '0') accum *= 2;
            else if (*cp == '1') accum = (accum * 2) + 1;
            else goto err;

            if (count++ >= 7) {
                *bp++ = cast(REBYTE, accum);
                count = 0;
                accum = 0;
            }
        }
        else if (!*cp || lex > LEX_DELIMIT_RETURN) goto err;
    }
    if (count) goto err; // improper modulus

    *bp = 0;
    SET_SERIES_LEN(ser, bp - BIN_HEAD(ser));
    ASSERT_SERIES_TERM(ser);
    return ser;

err:
    Free_Series(ser);
    *src = cp;
    return 0;
}


//
//  Decode_Base16: C
//
static REBSER *Decode_Base16(const REBYTE **src, REBCNT len, REBYTE delim)
{
    REBYTE *bp;
    const REBYTE *cp;
    REBCNT count = 0;
    REBCNT accum = 0;
    REBYTE lex;
    REBINT val;
    REBSER *ser;

    ser = Make_Binary(len / 2);
    bp = BIN_HEAD(ser);
    cp = *src;

    for (; len > 0; cp++, len--) {

        if (delim && *cp == delim) break;

        lex = Lex_Map[*cp];

        if (lex > LEX_WORD) {
            val = lex & LEX_VALUE; // char num encoded into lex
            if (!val && lex < LEX_NUMBER) goto err;  // invalid char (word but no val)
            accum = (accum << 4) + val;
            if (count++ & 1) *bp++ = cast(REBYTE, accum);
        }
        else if (!*cp || lex > LEX_DELIMIT_RETURN) goto err;
    }
    if (count & 1) goto err; // improper modulus

    *bp = 0;
    SET_SERIES_LEN(ser, bp - BIN_HEAD(ser));
    ASSERT_SERIES_TERM(ser);
    return ser;

err:
    Free_Series(ser);
    *src = cp;
    return 0;
}


//
//  Decode_Base64: C
//
static REBSER *Decode_Base64(const REBYTE **src, REBCNT len, REBYTE delim)
{
    REBYTE *bp;
    const REBYTE *cp;
    REBCNT flip = 0;
    REBCNT accum = 0;
    REBYTE lex;
    REBSER *ser;

    // Allocate buffer large enough to hold result:
    // Accounts for e bytes decoding into 3 bytes.
    ser = Make_Binary(((len + 3) * 3) / 4);
    bp = BIN_HEAD(ser);
    cp = *src;

    for (; len > 0; cp++, len--) {

        // Check for terminating delimiter (optional):
        if (delim && *cp == delim) break;

        // Check for char out of range:
        if (*cp > 127) {
            if (*cp == 0xA0) continue;  // hard space
            goto err;
        }

        lex = Debase64[*cp];

        if (lex < BIN_SPACE) {

            if (*cp != '=') {
                accum = (accum << 6) + lex;
                if (flip++ == 3) {
                    *bp++ = cast(REBYTE, accum >> 16);
                    *bp++ = cast(REBYTE, accum >> 8);
                    *bp++ = cast(REBYTE, accum);
                    accum = 0;
                    flip = 0;
                }
            } else {
                // Special padding: "="
                cp++;
                len--;
                if (flip == 3) {
                    *bp++ = cast(REBYTE, accum >> 10);
                    *bp++ = cast(REBYTE, accum >> 2);
                    flip = 0;
                }
                else if (flip == 2) {
                    if (!Skip_To_Byte(cp, cp + len, '=')) goto err;
                    cp++;
                    *bp++ = cast(REBYTE, accum >> 4);
                    flip = 0;
                }
                else goto err;
                break;
            }
        }
        else if (lex == BIN_ERROR) goto err;
    }

    if (flip) goto err;

    *bp = 0;
    SET_SERIES_LEN(ser, bp - BIN_HEAD(ser));
    ASSERT_SERIES_TERM(ser);
    return ser;

err:
    Free_Series(ser);
    *src = cp;
    return 0;
}


//
//  Decode_Binary: C
//
// Scan and convert a binary string.
//
const REBYTE *Decode_Binary(
    REBVAL *value,
    const REBYTE *src,
    REBCNT len,
    REBINT base,
    REBYTE delim
) {
    REBSER *ser = 0;

    switch (base) {
    case 64:
        ser = Decode_Base64(&src, len, delim);
        break;
    case 16:
        ser = Decode_Base16(&src, len, delim);
        break;
    case 2:
        ser = Decode_Base2 (&src, len, delim);
        break;
    }

    if (!ser) return 0;

    Init_Binary(value, ser);

    return src;
}


//
//  Encode_Base2: C
//
// Base2 encode a given series. Must be BYTES, not UNICODE.
//
REBSER *Encode_Base2(const REBYTE *src, REBCNT len, REBOOL brk)
{
    // Account for binary digits, lines, and extra syntax ("slop factor")
    //
    REBSER *s = Make_Unicode(8 * len + 2 * (len / 8) + 4);
    REBUNI *dest = UNI_HEAD(s);

    if (len == 0) { // return empty series if input was zero length
        TERM_SEQUENCE_LEN(s, 0);
        return s;
    }

    if (len > 8 && brk)
        *dest++ = LF;

    REBCNT i;
    for (i = 0; i < len; i++) {
        REBYTE b = src[i];

        REBCNT n;
        for (n = 0x80; n > 0; n = n >> 1)
            *dest++ = (b & n) ? '1' : '0';

        if ((i + 1) % 8 == 0 && brk)
            *dest++ = LF;
    }
    *dest = '\0';

    if (*(dest - 1) != LF && len > 9 && brk)
        *dest++ = LF;

    SET_SERIES_LEN(s, cast(REBCNT, dest - UNI_HEAD(s)));
    ASSERT_SERIES_TERM(s);
    return s;
}


//
//  Encode_Base16: C
//
// Base16 encode a given series. Must be BYTES, not UNICODE.
//
REBSER *Encode_Base16(const REBYTE *src, REBCNT len, REBOOL brk)
{
    // Account for hex digits, lines, and extra syntax ("slop factor")
    //
    REBSER *s = Make_Unicode(len * 2 + len / 32 + 32);
    REBUNI *dest = UNI_HEAD(s);

    if (len == 0) { // return empty series if input was zero length
        TERM_SEQUENCE_LEN(s, 0);
        return s;
    }

    if (len >= 32 && brk)
        *dest++ = LF;

    REBCNT count;
    for (count = 1; count <= len; count++) {
        dest = Form_Hex2_Uni(dest, *src++);
        if (brk && ((count % 32) == 0))
            *dest++ = LF;
    }

    if (*(dest - 1) != LF && (len >= 32) && brk)
        *dest++ = LF;

    *dest = '\0';

    SET_SERIES_LEN(s, cast(REBCNT, dest - UNI_HEAD(s)));
    ASSERT_SERIES_TERM(s);
    return s;
}


//
//  Encode_Base64: C
//
// Base64 encode a given series. Must be BYTES, not UNICODE.
//
REBSER *Encode_Base64(const REBYTE *src, REBCNT len, REBOOL brk)
{
    // Account for base64 digits, lines, and extra syntax ("slop factor")
    //
    REBSER *s = Make_Unicode(4 * len / 3 + 2 * (len / 32) + 5);
    REBUNI *dest = UNI_HEAD(s);

    if (len == 0) { // return empty series if input was zero length
        TERM_SEQUENCE_LEN(s, 0);
        return s;
    }

    REBINT loop = cast(int, len / 3) - 1;
    if (4 * loop > 64 && brk)
        *dest++ = LF;

    REBINT x;
    for (x = 0; x <= 3 * loop; x += 3) {
        *dest++ = Enbase64[src[x] >> 2];
        *dest++ = Enbase64[((src[x] & 0x3) << 4) + (src[x + 1] >> 4)];
        *dest++ = Enbase64[((src[x + 1] & 0xF) << 2) + (src[x + 2] >> 6)];
        *dest++ = Enbase64[(src[x + 2] % 0x40)];
        if ((x + 3) % 48 == 0 && brk)
            *dest++ = LF;
    }

    if ((len % 3) != 0) {
        dest[2] = dest[3] = '=';

        *dest++ = Enbase64[src[x] >> 2];

        if ((len - x) >= 1)
            *dest++ = Enbase64[
                ((src[x] & 0x3) << 4)
                + ((len - x) == 1 ? 0 : src[x + 1] >> 4)
            ];
        else
            dest++;

        if ((len - x) == 2)
            *dest++ = Enbase64[(src[x + 1] & 0xF) << 2];
        else
            dest++;

        dest++;
    }

    if (*(dest - 1) != LF && x > 49 && brk)
        *dest++ = LF;

    *dest = '\0';

    SET_SERIES_LEN(s, cast(REBCNT, dest - UNI_HEAD(s)));
    ASSERT_SERIES_TERM(s);
    return s;
}
