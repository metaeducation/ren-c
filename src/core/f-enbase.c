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
// Base-64 binary decoder table.
//
static const REBYTE Debase64[128] =
{
    #define BIN_ERROR   (REBYTE)0x80
    #define BIN_SPACE   (REBYTE)0x40
    #define BIN_VALUE   (REBYTE)0x3f
    #define IS_BIN_SPACE(c) (did (Debase64[c] & BIN_SPACE))

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
static REBBIN *Decode_Base2(const REBYTE **src, REBLEN len, REBYTE delim)
{
    REBLEN count = 0;
    REBLEN accum = 0;

    REBBIN *bin = Make_Binary(len >> 3);
    REBYTE *bp = BIN_HEAD(bin);
    const REBYTE *cp = *src;

    for (; len > 0; cp++, len--) {

        if (delim && *cp == delim) break;

        REBYTE lex = Lex_Map[*cp];

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

    TERM_BIN_LEN(bin, bp - BIN_HEAD(bin));
    return bin;

err:
    Free_Unmanaged_Series(bin);
    *src = cp;
    return 0;
}


//
//  Decode_Base16: C
//
static REBBIN *Decode_Base16(const REBYTE **src, REBLEN len, REBYTE delim)
{
    REBLEN count = 0;
    REBLEN accum = 0;

    REBBIN *bin = Make_Binary(len / 2);
    REBYTE *bp = BIN_HEAD(bin);
    const REBYTE *cp = *src;

    for (; len > 0; cp++, len--) {

        if (delim && *cp == delim) break;

        REBYTE lex = Lex_Map[*cp];

        if (lex > LEX_WORD) {
            REBINT val = lex & LEX_VALUE;  // char num encoded into lex
            if (!val && lex < LEX_NUMBER) goto err;  // invalid char (word but no val)
            accum = (accum << 4) + val;
            if (count++ & 1) *bp++ = cast(REBYTE, accum);
        }
        else if (!*cp || lex > LEX_DELIMIT_RETURN) goto err;
    }
    if (count & 1) goto err; // improper modulus

    TERM_BIN_LEN(bin, bp - BIN_HEAD(bin));
    return bin;

err:
    Free_Unmanaged_Series(bin);
    *src = cp;
    return 0;
}


//
//  Decode_Base64: C
//
static REBBIN *Decode_Base64(const REBYTE **src, REBLEN len, REBYTE delim)
{
    REBLEN flip = 0;
    REBLEN accum = 0;

    // Allocate buffer large enough to hold result:
    // Accounts for e bytes decoding into 3 bytes.

    REBBIN *bin = Make_Binary(((len + 3) * 3) / 4);
    REBYTE *bp = BIN_HEAD(bin);
    const REBYTE *cp = *src;

    for (; len > 0; cp++, len--) {

        // Check for terminating delimiter (optional):
        if (delim && *cp == delim) break;

        // Check for char out of range:
        if (*cp > 127) {
            if (*cp == 0xA0) continue;  // hard space
            goto err;
        }

        REBYTE lex = Debase64[*cp];

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

    TERM_BIN_LEN(bin, bp - BIN_HEAD(bin));
    return bin;

err:
    Free_Unmanaged_Series(bin);
    *src = cp;
    return 0;
}


//
//  Decode_Binary: C
//
// Scan and convert a binary string.
//
const REBYTE *Decode_Binary(
    Cell *out,
    const REBYTE *src,
    REBLEN len,
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

    Init_Binary(out, ser);

    return src;
}


//
//  Form_Base2: C
//
// Base2 encode a range of arbitrary bytes into a byte-sized ASCII series.
//
void Form_Base2(REB_MOLD *mo, const REBYTE *src, REBLEN len, bool brk)
{
    if (len == 0)
        return;

    // !!! This used to predict the length, accounting for hex digits, lines,
    // and extra syntax ("slop factor"):
    //
    //     8 * len + 2 * (len / 8) + 4

    if (len > 8 && brk)
        Append_Codepoint(mo->series, LF);

    REBLEN i;
    for (i = 0; i < len; i++) {
        REBYTE b = src[i];

        REBLEN n;
        for (n = 0x80; n > 0; n = n >> 1)
            Append_Codepoint(mo->series, (b & n) ? '1' : '0');

        if ((i + 1) % 8 == 0 && brk)
            Append_Codepoint(mo->series, LF);
    }

    if (*BIN_TAIL(mo->series) != LF && len > 9 && brk)
        Append_Codepoint(mo->series, LF);
}


//
//  Form_Base16: C
//
// Base16 encode a range of arbitrary bytes into a byte-sized ASCII series.
//
void Form_Base16(REB_MOLD *mo, const REBYTE *src, REBLEN len, bool brk)
{
    if (len == 0)
        return;

    // !!! This used to predict the length, accounting for hex digits, lines,
    // and extra syntax ("slop factor"):
    //
    //     len * 2 + len / 32 + 32

    if (brk and len >= 32)
        Append_Codepoint(mo->series, LF);

    REBLEN count;
    for (count = 1; count <= len; count++) {
        Form_Hex2(mo, *src++);
        if (brk and ((count % 32) == 0))
            Append_Codepoint(mo->series, LF);
    }

    if (brk and (len >= 32) and *BIN_LAST(mo->series) != LF)
        Append_Codepoint(mo->series, LF);
}


//
//  Form_Base64: C
//
// Base64 encode a range of arbitrary bytes into a byte-sized ASCII series.
//
// !!! Strongly parallels this code, may have originated from it:
// http://web.mit.edu/freebsd/head/contrib/wpa/src/utils/base64.c
//
void Form_Base64(REB_MOLD *mo, const REBYTE *src, REBLEN len, bool brk)
{
    // !!! This used to predict the length, accounting for hex digits, lines,
    // and extra syntax ("slop factor") and preallocate size for that.  Now
    // it appends one character at a time and relies upon the mold buffer's
    // natural expansion.  Review if it needs the optimization.

    REBSTR *s = mo->series;

    REBINT loop = cast(int, len / 3) - 1;
    if (brk and 4 * loop > 64)
        Append_Codepoint(s, LF);

    REBINT x;
    for (x = 0; x <= 3 * loop; x += 3) {
        Append_Codepoint(s, Enbase64[src[x] >> 2]);
        Append_Codepoint(
            s,
            Enbase64[((src[x] & 0x3) << 4) + (src[x + 1] >> 4)]
        );
        Append_Codepoint(
            s,
            Enbase64[((src[x + 1] & 0xF) << 2) + (src[x + 2] >> 6)]
        );
        Append_Codepoint(s, Enbase64[(src[x + 2] % 0x40)]);
        if ((x + 3) % 48 == 0 && brk)
            Append_Codepoint(s, LF);
    }

    if ((len % 3) != 0) {
        Append_Codepoint(s, Enbase64[src[x] >> 2]);

        if (len - x == 1) {
            Append_Codepoint(s, Enbase64[((src[x] & 0x3) << 4)]);
            Append_Codepoint(s, '=');
        }
        else {
            Append_Codepoint(
                s,
                Enbase64[((src[x] & 0x3) << 4) | (src[x + 1] >> 4)]
            );
            Append_Codepoint(s, Enbase64[(src[x + 1] & 0xF) << 2]);
        }

        Append_Codepoint(s, '=');
    }

    if (brk and x > 49 and *BIN_LAST(s) != LF)
        Append_Codepoint(s, LF);
}
