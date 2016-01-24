/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2012 REBOL Technologies
**  REBOL is a trademark of REBOL Technologies
**
**  Licensed under the Apache License, Version 2.0 (the "License");
**  you may not use this file except in compliance with the License.
**  You may obtain a copy of the License at
**
**  http://www.apache.org/licenses/LICENSE-2.0
**
**  Unless required by applicable law or agreed to in writing, software
**  distributed under the License is distributed on an "AS IS" BASIS,
**  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**  See the License for the specific language governing permissions and
**  limitations under the License.
**
************************************************************************
**
**  Module:  l-scan.c
**  Summary: lexical analyzer for source to binary translation
**  Section: lexical
**  Author:  Carl Sassenrath
**  Notes:
**    WARNING WARNING WARNING
**    This is highly tuned code that should only be modified by experts
**    who fully understand its design. It is very easy to create odd
**    side effects so please be careful and extensively test all changes!
**
***********************************************************************/

#include "sys-core.h"

// In UTF8 C0, C1, F5, and FF are invalid.
#ifdef USE_UNICODE
#define LEX_UTFE LEX_DEFAULT
#else
#define LEX_UTFE LEX_WORD
#endif

//
// Maps each character to its lexical attributes, using
// a frequency optimized encoding.
//
// UTF8: The values C0, C1, F5 to FF never appear.
//
const REBYTE Lex_Map[256] =
{
    /* 00 EOF */    LEX_DELIMIT|LEX_DELIMIT_END,
    /* 01     */    LEX_DEFAULT,
    /* 02     */    LEX_DEFAULT,
    /* 03     */    LEX_DEFAULT,
    /* 04     */    LEX_DEFAULT,
    /* 05     */    LEX_DEFAULT,
    /* 06     */    LEX_DEFAULT,
    /* 07     */    LEX_DEFAULT,
    /* 08 BS  */    LEX_DEFAULT,
    /* 09 TAB */    LEX_DEFAULT,
    /* 0A LF  */    LEX_DELIMIT|LEX_DELIMIT_LINEFEED,
    /* 0B     */    LEX_DEFAULT,
    /* 0C PG  */    LEX_DEFAULT,
    /* 0D CR  */    LEX_DELIMIT|LEX_DELIMIT_RETURN,
    /* 0E     */    LEX_DEFAULT,
    /* 0F     */    LEX_DEFAULT,

    /* 10     */    LEX_DEFAULT,
    /* 11     */    LEX_DEFAULT,
    /* 12     */    LEX_DEFAULT,
    /* 13     */    LEX_DEFAULT,
    /* 14     */    LEX_DEFAULT,
    /* 15     */    LEX_DEFAULT,
    /* 16     */    LEX_DEFAULT,
    /* 17     */    LEX_DEFAULT,
    /* 18     */    LEX_DEFAULT,
    /* 19     */    LEX_DEFAULT,
    /* 1A     */    LEX_DEFAULT,
    /* 1B     */    LEX_DEFAULT,
    /* 1C     */    LEX_DEFAULT,
    /* 1D     */    LEX_DEFAULT,
    /* 1E     */    LEX_DEFAULT,
    /* 1F     */    LEX_DEFAULT,

    /* 20     */    LEX_DELIMIT|LEX_DELIMIT_SPACE,
    /* 21 !   */    LEX_WORD,
    /* 22 "   */    LEX_DELIMIT|LEX_DELIMIT_DOUBLE_QUOTE,
    /* 23 #   */    LEX_SPECIAL|LEX_SPECIAL_POUND,
    /* 24 $   */    LEX_SPECIAL|LEX_SPECIAL_DOLLAR,
    /* 25 %   */    LEX_SPECIAL|LEX_SPECIAL_PERCENT,
    /* 26 &   */    LEX_WORD,
    /* 27 '   */    LEX_SPECIAL|LEX_SPECIAL_APOSTROPHE,
    /* 28 (   */    LEX_DELIMIT|LEX_DELIMIT_LEFT_PAREN,
    /* 29 )   */    LEX_DELIMIT|LEX_DELIMIT_RIGHT_PAREN,
    /* 2A *   */    LEX_WORD,
    /* 2B +   */    LEX_SPECIAL|LEX_SPECIAL_PLUS,
    /* 2C ,   */    LEX_SPECIAL|LEX_SPECIAL_COMMA,
    /* 2D -   */    LEX_SPECIAL|LEX_SPECIAL_MINUS,
    /* 2E .   */    LEX_SPECIAL|LEX_SPECIAL_PERIOD,
    /* 2F /   */    LEX_DELIMIT|LEX_DELIMIT_SLASH,

    /* 30 0   */    LEX_NUMBER|0,
    /* 31 1   */    LEX_NUMBER|1,
    /* 32 2   */    LEX_NUMBER|2,
    /* 33 3   */    LEX_NUMBER|3,
    /* 34 4   */    LEX_NUMBER|4,
    /* 35 5   */    LEX_NUMBER|5,
    /* 36 6   */    LEX_NUMBER|6,
    /* 37 7   */    LEX_NUMBER|7,
    /* 38 8   */    LEX_NUMBER|8,
    /* 39 9   */    LEX_NUMBER|9,
    /* 3A :   */    LEX_SPECIAL|LEX_SPECIAL_COLON,
    /* 3B ;   */    LEX_DELIMIT|LEX_DELIMIT_SEMICOLON,
    /* 3C <   */    LEX_SPECIAL|LEX_SPECIAL_LESSER,
    /* 3D =   */    LEX_WORD,
    /* 3E >   */    LEX_SPECIAL|LEX_SPECIAL_GREATER,
    /* 3F ?   */    LEX_WORD,

    /* 40 @   */    LEX_SPECIAL|LEX_SPECIAL_AT,
    /* 41 A   */    LEX_WORD|10,
    /* 42 B   */    LEX_WORD|11,
    /* 43 C   */    LEX_WORD|12,
    /* 44 D   */    LEX_WORD|13,
    /* 45 E   */    LEX_WORD|14,
    /* 46 F   */    LEX_WORD|15,
    /* 47 G   */    LEX_WORD,
    /* 48 H   */    LEX_WORD,
    /* 49 I   */    LEX_WORD,
    /* 4A J   */    LEX_WORD,
    /* 4B K   */    LEX_WORD,
    /* 4C L   */    LEX_WORD,
    /* 4D M   */    LEX_WORD,
    /* 4E N   */    LEX_WORD,
    /* 4F O   */    LEX_WORD,

    /* 50 P   */    LEX_WORD,
    /* 51 Q   */    LEX_WORD,
    /* 52 R   */    LEX_WORD,
    /* 53 S   */    LEX_WORD,
    /* 54 T   */    LEX_WORD,
    /* 55 U   */    LEX_WORD,
    /* 56 V   */    LEX_WORD,
    /* 57 W   */    LEX_WORD,
    /* 58 X   */    LEX_WORD,
    /* 59 Y   */    LEX_WORD,
    /* 5A Z   */    LEX_WORD,
    /* 5B [   */    LEX_DELIMIT|LEX_DELIMIT_LEFT_BRACKET,
    /* 5C \   */    LEX_SPECIAL|LEX_SPECIAL_BACKSLASH,
    /* 5D ]   */    LEX_DELIMIT|LEX_DELIMIT_RIGHT_BRACKET,
    /* 5E ^   */    LEX_WORD,
    /* 5F _   */    LEX_WORD,

    /* 60 `   */    LEX_WORD,
    /* 61 a   */    LEX_WORD|10,
    /* 62 b   */    LEX_WORD|11,
    /* 63 c   */    LEX_WORD|12,
    /* 64 d   */    LEX_WORD|13,
    /* 65 e   */    LEX_WORD|14,
    /* 66 f   */    LEX_WORD|15,
    /* 67 g   */    LEX_WORD,
    /* 68 h   */    LEX_WORD,
    /* 69 i   */    LEX_WORD,
    /* 6A j   */    LEX_WORD,
    /* 6B k   */    LEX_WORD,
    /* 6C l   */    LEX_WORD,
    /* 6D m   */    LEX_WORD,
    /* 6E n   */    LEX_WORD,
    /* 6F o   */    LEX_WORD,

    /* 70 p   */    LEX_WORD,
    /* 71 q   */    LEX_WORD,
    /* 72 r   */    LEX_WORD,
    /* 73 s   */    LEX_WORD,
    /* 74 t   */    LEX_WORD,
    /* 75 u   */    LEX_WORD,
    /* 76 v   */    LEX_WORD,
    /* 77 w   */    LEX_WORD,
    /* 78 x   */    LEX_WORD,
    /* 79 y   */    LEX_WORD,
    /* 7A z   */    LEX_WORD,
    /* 7B {   */    LEX_DELIMIT|LEX_DELIMIT_LEFT_BRACE,
    /* 7C |   */    LEX_SPECIAL|LEX_SPECIAL_BAR,
    /* 7D }   */    LEX_DELIMIT|LEX_DELIMIT_RIGHT_BRACE,
    /* 7E ~   */    LEX_WORD,  //LEX_SPECIAL|LEX_SPECIAL_TILDE,
    /* 7F DEL */    LEX_DEFAULT,

    /* Odd Control Chars */
    LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,    /* 80 */
    LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
    LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
    LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
    LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
    LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
    LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
    LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,

    /* Alternate Chars */
#ifdef USE_UNICODE
    LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
#else
    LEX_DEFAULT,LEX_WORD,LEX_WORD,LEX_WORD, /* A0 (a space) */
#endif
    LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
    LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
    LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,

    LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
    LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
    LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
    LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,

    // C0, C1
    LEX_UTFE,LEX_UTFE,LEX_WORD,LEX_WORD,
    LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
    LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
    LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,

    LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
    LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
    LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
    LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,

    LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
    LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
    LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
    LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,

    LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
    LEX_WORD,LEX_UTFE,LEX_WORD,LEX_WORD,
    LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
    LEX_WORD,LEX_WORD,LEX_WORD,LEX_UTFE
};

#ifdef LOWER_CASE_BYTE
//
// Maps each character to its upper case value.  Done this
// way for speed.  Note the odd cases in last block.
//
const REBYTE Upper_Case[256] =
{
      0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
     16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
     32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
     48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,

     64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
     80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95,
     96, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
     80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90,123,124,125,126,127,

    128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,
    // some up/low cases mod 16 (not mod 32)
    144,145,146,147,148,149,150,151,152,153,138,155,156,141,142,159,
    160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,
    176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,

    192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,
    208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223,
    192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,
    208,209,210,211,212,213,214,247,216,217,218,219,220,221,222,159
};


//
// Maps each character to its lower case value.  Done this
// way for speed.  Note the odd cases in last block.
//
const REBYTE Lower_Case[256] =
{
      0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
     16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
     32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
     48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,

     64, 97, 98, 99,100,101,102,103,104,105,106,107,108,109,110,111,
    112,113,114,115,116,117,118,119,120,121,122, 91, 92, 93, 94, 95,
     96, 97, 98, 99,100,101,102,103,104,105,106,107,108,109,110,111,
    112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127,

    128,129,130,131,132,133,134,135,136,137,154,139,140,157,158,143,
    // some up/low cases mod 16 (not mod 32)
    144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,255,
    160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,
    176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,

    224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,
    240,241,242,243,244,245,246,215,248,249,250,251,252,253,254,223,
    224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,
    240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255
};
#endif


//
//  Skip_To_Byte: C
// 
// Skip to the specified byte but not past the provided end
// pointer of the byte string.  Return NULL if byte is not found.
//
const REBYTE *Skip_To_Byte(const REBYTE *cp, const REBYTE *ep, REBYTE b)
{
    while (cp != ep && *cp != b) cp++;
    if (*cp == b) return cp;
    return NULL;
}


//
//  Scan_UTF8_Char_Escapable: C
// 
// Scan a char, handling ^A, ^/, ^(null), ^(1234)
// 
// Returns the numeric value for char, or NULL for errors.
// 0 is a legal codepoint value which may be returned.
// 
// Advances the cp to just past the last position.
// 
// test: to-integer load to-binary mold to-char 1234
//
static const REBYTE *Scan_UTF8_Char_Escapable(REBUNI *out, const REBYTE *bp)
{
    const REBYTE *cp;
    REBYTE c;
    REBYTE lex;

    c = *bp;

    // Handle unicoded char:
    if (c >= 0x80) {
        if (!(bp = Back_Scan_UTF8_Char(out, bp, NULL))) return NULL;
        return bp + 1; // Back_Scan advances one less than the full encoding
    }

    bp++;

    if (c != '^') {
        *out = c;
        return bp;
    }

    // Must be ^ escaped char:
    c = *bp;
    bp++;

    switch (c) {

    case 0:
        *out = 0;
        break;

    case '/':
        *out = LF;
        break;

    case '^':
        *out = c;
        break;

    case '-':
        *out = TAB;
        break;

    case '!':
        *out = '\036'; // record separator
        break;

    case '(':   // ^(tab) ^(1234)
        // Check for hex integers ^(1234):
        cp = bp; // restart location
        *out = 0;
        while ((lex = Lex_Map[*cp]) > LEX_WORD) {
            c = lex & LEX_VALUE;
            if (!c && lex < LEX_NUMBER) break;
            *out = (*out << 4) + c;
            cp++;
        }
        if ((cp - bp) > 4) return NULL;
        if (*cp == ')') {
            cp++;
            return cp;
        }

        // Check for identifiers:
        for (c = 0; c < ESC_MAX; c++) {
            if ((cp = Match_Bytes(bp, cb_cast(Esc_Names[c])))) {
                if (cp && *cp == ')') {
                    bp = cp + 1;
                    *out = Esc_Codes[c];
                    return bp;
                }
            }
        }
        return NULL;

    default:
        *out = c;

        c = UP_CASE(c);
        if (c >= '@' && c <= '_') *out = c - '@';
        else if (c == '~') *out = 0x7f; // special for DEL
        else {
            // keep original `c` value before UP_CASE (includes: ^{ ^} ^")
        }
    }

    return bp;
}


//
//  Scan_Quote_Push_Mold: C
// 
// Scan a quoted string, handling all the escape characters.
// 
// The result will be put into the temporary unistring mold buffer.
//
static const REBYTE *Scan_Quote_Push_Mold(
    REB_MOLD *mo,
    const REBYTE *src,
    SCAN_STATE *scan_state
) {
    REBINT nest = 0;
    REBUNI term;
    REBUNI chr;
    REBCNT lines = 0;

    Push_Mold(mo);

    term = (*src++ == '{') ? '}' : '"'; // pick termination

    while (*src != term || nest > 0) {

        chr = *src;

        switch (chr) {

        case 0:
            return 0; // Scan_state shows error location.

        case '^':
            if (!(src = Scan_UTF8_Char_Escapable(&chr, src))) return NULL;
            src--;
            break;

        case '{':
            if (term != '"') nest++;
            break;

        case '}':
            if (term != '"' && nest > 0) nest--;
            break;

        case CR:
            if (src[1] == LF) src++;
            // fall thru
        case LF:
            if (term == '"') return 0;
            lines++;
            chr = LF;
            break;

        default:
            if (chr >= 0x80) {
                if (!(src = Back_Scan_UTF8_Char(&chr, src, NULL)))
                    return NULL;
            }
        }

        src++;

        if (SER_LEN(mo->series) + 1 >= SER_REST(mo->series)) // incl term
            Extend_Series(mo->series, 1);

        *UNI_TAIL(mo->series) = chr;

        SET_SERIES_LEN(mo->series, SER_LEN(mo->series) + 1);
    }

    src++; // Skip ending quote or brace.

    if (scan_state) scan_state->line_count += lines;

    UNI_TERM(mo->series);

    return src;
}


//
//  Scan_Item_Push_Mold: C
// 
// Scan as UTF8 an item like a file or URL.
// 
// Returns continuation point or zero for error.
// 
// Put result into the temporary mold buffer as uni-chars.
//
const REBYTE *Scan_Item_Push_Mold(
    REB_MOLD *mo,
    const REBYTE *src,
    const REBYTE *end,
    REBUNI term,
    const REBYTE *invalid
) {
    REBUNI c;

    Push_Mold(mo);

    while (src < end && *src != term) {

        c = *src;

        // End of stream?
        if (c == 0) break;

        // If no term, then any white will terminate:
        if (!term && IS_WHITE(c)) break;

        // Ctrl chars are invalid:
        if (c < ' ') return 0;  // invalid char

        if (c == '\\') c = '/';

        // Accept %xx encoded char:
        else if (c == '%') {
            if (!Scan_Hex2(src+1, &c, FALSE)) return 0;
            src += 2;
        }

        // Accept ^X encoded char:
        else if (c == '^') {
            if (src+1 == end) return 0; // nothing follows ^
            if (!(src = Scan_UTF8_Char_Escapable(&c, src))) return NULL;
            if (!term && IS_WHITE(c)) break;
            src--;
        }

        // Accept UTF8 encoded char:
        else if (c >= 0x80) {
            if (!(src = Back_Scan_UTF8_Char(&c, src, 0))) return NULL;
        }

        // Is char as literal valid? (e.g. () [] etc.)
        else if (invalid && strchr(cs_cast(invalid), c)) return 0;

        src++;

        *UNI_TAIL(mo->series) = c; // not affected by Extend_Series

        SET_SERIES_LEN(mo->series, SER_LEN(mo->series) + 1);

        if (SER_LEN(mo->series) >= SER_REST(mo->series))
            Extend_Series(mo->series, 1);
    }

    if (*src && *src == term) src++;

    UNI_TERM(mo->series);

    return src;
}


//
//  Skip_Tag: C
// 
// Skip the entire contents of a tag, including quoted strings.
// The argument points to the opening '<'.  Zero is returned on
// errors.
//
static const REBYTE *Skip_Tag(const REBYTE *cp)
{
    if (*cp == '<') cp++;
    while (*cp && *cp != '>') {
        if (*cp == '"') {
            cp++;
            while (*cp && *cp != '"') cp++;
            if (!*cp) return 0;
        }
        cp++;
    }
    if (*cp) return cp+1;
    return 0;
}


//
//  Error_Bad_Scan: C
// 
// Scanner error handler
//
static REBCTX *Error_Bad_Scan(
    REBCNT errnum,
    SCAN_STATE *ss,
    REBCNT tkn,
    const REBYTE *arg,
    REBCNT size
) {
    REBCTX *error;

    const REBYTE *name;
    const REBYTE *cp;
    const REBYTE *bp;
    REBSER *ser;
    REBCNT len = 0;

    ERROR_OBJ *err_obj;
    REBVAL arg1;
    REBVAL arg2;
    VAL_INIT_WRITABLE_DEBUG(&arg1);
    VAL_INIT_WRITABLE_DEBUG(&arg2);

    assert(errnum != 0);

    ss->errors++;

    if (PG_Boot_Strs)
        name = BOOT_STR(RS_SCAN,tkn);
    else
        name = cb_cast("boot");

    cp = ss->head_line;
    while (IS_LEX_SPACE(*cp)) cp++; // skip indentation
    bp = cp;
    while (!ANY_CR_LF_END(*cp)) {
        cp++;
        len++;
    }

    ser = Make_Binary(len + 16);
    Append_Unencoded(ser, "(line ");
    Append_Int(ser, ss->line_count);
    Append_Unencoded(ser, ") ");
    Append_Series(ser, bp, len);

    Val_Init_String(&arg1, Copy_Bytes(name, -1));
    Val_Init_String(&arg2, Copy_Bytes(arg, size));

    error = Error(errnum, &arg1, &arg2, NULL);

    // Write the NEAR information (`Error()` gets it from DSF)
    //
    err_obj = cast(ERROR_OBJ*, ARR_HEAD(CTX_VARLIST(error)));
    Val_Init_String(&err_obj->nearest, ser);

    return error;
}


//
//  Prescan_Token: C
// 
// This function updates `scan_state->begin` to skip past leading
// whitespace.  If the first character it finds after that is a
// LEX_DELIMITER (`"`, `[`, `)`, `{`, etc. or a space/newline)
// then it will advance the end position to just past that one
// character.  For all other leading characters, it will advance
// the end pointer up to the first delimiter class byte (but not
// include it.)
// 
// If the first character is not a delimiter, then this routine
// also gathers a quick "fingerprint" of the special characters
// that appeared after it, but before a delimiter was found.
// This comes from unioning LEX_SPECIAL_XXX flags of the bytes
// that are seen (plus LEX_SPECIAL_WORD if any legal word bytes
// were found in that range.)
// 
// So if the input were "$#foobar[@" this would come back with
// the flags LEX_SPECIAL_POUND and LEX_SPECIAL_WORD set.  Since
// it is the first character, the `$` would not be counted to
// add LEX_SPECIAL_DOLLAR.  And LEX_SPECIAL_AT would not be set
// even though there is an `@` character, because it occurs
// after the `[` which is LEX_DELIMITER class.
// 
// Note: The reason the first character's lexical class is not
// considered is because it's important to know it exactly, so
// the caller will use GET_LEX_CLASS(scan_state->begin[0]).
// Fingerprinting just helps accelerate further categorization.
//
static REBCNT Prescan_Token(SCAN_STATE *scan_state)
{
    const REBYTE *cp = scan_state->begin;
    REBCNT flags = 0;

    // Skip whitespace (if any) and update the scan_state
    while (IS_LEX_SPACE(*cp)) cp++;
    scan_state->begin = cp;

    while (TRUE) {
        switch (GET_LEX_CLASS(*cp)) {

        case LEX_CLASS_DELIMIT:
            if (cp == scan_state->begin) {
                // Include the delimiter if it is the only character we
                // are returning in the range (leave it out otherwise)
                scan_state->end = cp + 1;

                // Note: We'd liked to have excluded LEX_DELIMIT_END, but
                // would require a GET_LEX_VALUE() call to know to do so.
                // Locate_Token_May_Push_Mold() does a `switch` on that,
                // so it can subtract this addition back out itself.
            }
            else
                scan_state->end = cp;
            return flags;

        case LEX_CLASS_SPECIAL:
            if (cp != scan_state->begin) {
                // As long as it isn't the first character, we union a flag
                // in the result mask to signal this special char's presence
                SET_LEX_FLAG(flags, GET_LEX_VALUE(*cp));
            }
            cp++;
            break;

        case LEX_CLASS_WORD:
            // !!! Comment said "flags word char (for nums)"...meaning?
            SET_LEX_FLAG(flags, LEX_SPECIAL_WORD);
            while (IS_LEX_WORD_OR_NUMBER(*cp)) cp++;
            break;

        case LEX_CLASS_NUMBER:
            while (IS_LEX_NUMBER(*cp)) cp++;
            break;
        }
    }
}


//
//  Locate_Token_May_Push_Mold: C
// 
// Find the beginning and end character pointers for the next
// TOKEN_ in the scanner state.  The TOKEN_ type returned will
// correspond directly to a Rebol datatype if it isn't an
// ANY-ARRAY! (e.g. TOKEN_INTEGER for INTEGER! or TOKEN_STRING
// for STRING!).  When a block or group delimiter was found it
// will indicate that (e.g. TOKEN_BLOCK_BEGIN or TOKEN_GROUP_END).
// Hence the routine will have to be called multiple times during
// the array's content scan.
// 
// !!! This should be modified to explain how paths work, once
// I can understand how paths work. :-/  --HF
// 
// The scan_state will be updated so that `scan_state->begin`
// has been moved past any leading whitespace that was pending in
// the buffer.  `scan_state->end` will hold the conclusion at
// a delimiter.  TOKEN_END is returned if end of input is reached
// (signaled by a null byte).
// 
// Newlines that should be internal to a non-ANY-ARRAY! type are
// included in the scanned range between the `begin` and `end`.
// But newlines that are found outside of a string are returned
// as TOKEN_NEWLINE.  (These are used to set the OPTS_VALUE_LINE
// formatting bit on the values.)
// 
// Determining the end point of token types that need escaping
// requires processing (for instance `{a^}b}` can't see the first
// close brace as ending the string).  To avoid double processing,
// the routine decodes the string's content into UNI_BUF for any
// quoted form to be used by the caller.  This is overwritten in
// successive calls, and is only done for quoted forms (e.g. %"foo"
// will have data in UNI_BUF but %foo will not.)
// 
// !!! This is a somewhat weird separation of responsibilities,
// that seems to arise from a desire to make "Scan_XXX" functions
// independent of the "Locate_Token_May_Push_Mold" function.  But if the work of
// locating the value means you have to basically do what you'd
// do to read it into a REBVAL anyway, why split it up?  --HF
// 
// Error handling is limited for most types, as an additional
// phase is needed to load their data into a REBOL value.  Yet if
// a "cheap" error is incidentally found during this routine
// without extra cost to compute, it is indicated by returning a
// negative value for the malformed type.
// 
// !!! What real value is this optimization of the negative type,
// as opposed to just raising the error here?  Is it required to
// support a resumable scanner on partially written source, such
// as in a syntax highlighter?  Is the "relaxed" mode of handling
// errors already sufficient to achieve the goal?  --HF
// 
// Examples with scan_state's (B)egin (E)nd and return value:
// 
//        foo: baz bar => TOKEN_SET
//        B   E
// 
//     [quick brown fox] => TOKEN_BLOCK_BEGIN
//     B
//      E
// 
//     "brown fox]" => TOKEN_WORD
//      B    E
// 
//       $10AE.20 sent => -TOKEN_MONEY (negative, malformed)
//       B       E
// 
//       {line1\nline2}  => TOKEN_STRING (content in UNI_BUF)
//       B             E
// 
//     \n{line2} => TOKEN_NEWLINE (newline is external)
//     BB
//       E
// 
//     %"a ^"b^" c" d => TOKEN_FILE (content in UNI_BUF)
//     B           E
// 
//     %a-b.c d => TOKEN_FILE (content *not* in UNI_BUF)
//     B     E
// 
//     \0 => TOKEN_END
//     BB
//     EE
// 
// Note: The reason that the code is able to use byte scanning
// over UTF-8 encoded source is because all the characters
// that dictate the tokenization are ASCII (< 128).
//
static REBINT Locate_Token_May_Push_Mold(REB_MOLD *mo, SCAN_STATE *scan_state)
{
    REBCNT flags = Prescan_Token(scan_state);

    const REBYTE *cp = scan_state->begin;

    REBINT type;

    switch (GET_LEX_CLASS(*cp)) {

    case LEX_CLASS_DELIMIT:
        switch (GET_LEX_VALUE(*cp)) {
        case LEX_DELIMIT_SPACE:
            // We should not get whitespace as Prescan_Token skips it all
            assert(FALSE);
            DEAD_END;

        case LEX_DELIMIT_SEMICOLON:     /* ; begin comment */
            while (!ANY_CR_LF_END(*cp)) cp++;
            if (!*cp) cp--;             /* avoid passing EOF  */
            if (*cp == LF) goto line_feed;
            /* fall thru  */
        case LEX_DELIMIT_RETURN:
            if (cp[1] == LF) cp++;
            /* fall thru */
        case LEX_DELIMIT_LINEFEED:
        line_feed:
            scan_state->line_count++;
            scan_state->end = cp + 1;
            return TOKEN_NEWLINE;


        // [BRACKETS]

        case LEX_DELIMIT_LEFT_BRACKET:
            return TOKEN_BLOCK_BEGIN;

        case LEX_DELIMIT_RIGHT_BRACKET:
            return TOKEN_BLOCK_END;


        // (PARENS)

        case LEX_DELIMIT_LEFT_PAREN:
            return TOKEN_PAREN_BEGIN;

        case LEX_DELIMIT_RIGHT_PAREN:
            return TOKEN_PAREN_END;


        // "QUOTES" and {BRACES}

        case LEX_DELIMIT_DOUBLE_QUOTE:
            cp = Scan_Quote_Push_Mold(mo, cp, scan_state);
            goto check_str;

        case LEX_DELIMIT_LEFT_BRACE:
            cp = Scan_Quote_Push_Mold(mo, cp, scan_state);
        check_str:
            if (cp) {
                scan_state->end = cp;
                return TOKEN_STRING;
            }
            // try to recover at next new line...
            cp = scan_state->begin + 1;
            while (!ANY_CR_LF_END(*cp)) cp++;
            scan_state->end = cp;
            return -TOKEN_STRING;

        case LEX_DELIMIT_RIGHT_BRACE:
            // !!! handle better (missing)
            return -TOKEN_STRING;


        // /SLASH

        case LEX_DELIMIT_SLASH:
            while (*cp && *cp == '/') cp++;
            if (
                IS_LEX_WORD_OR_NUMBER(*cp)
                || *cp == '+'
                || *cp == '-'
                || *cp == '.'
            ) {
                // ///refine not allowed
                if (scan_state->begin + 1 != cp) {
                    scan_state->end = cp;
                    return -TOKEN_REFINE;
                }
                scan_state->begin = cp;
                flags = Prescan_Token(scan_state);
                scan_state->begin--;
                type = TOKEN_REFINE;
                // Fast easy case:
                if (ONLY_LEX_FLAG(flags, LEX_SPECIAL_WORD)) return type;
                goto scanword;
            }
            if (cp[0] == '<' || cp[0] == '>') {
                scan_state->end = cp + 1;
                return -TOKEN_REFINE;
            }
            scan_state->end = cp;
            return TOKEN_WORD;

        case LEX_DELIMIT_END:
            // Prescan_Token() spans the terminator as if it were a byte
            // to process, so we collapse end to begin to signal no data
            scan_state->end--;
            assert(scan_state->end == scan_state->begin);
            return TOKEN_END;

        case LEX_DELIMIT_UTF8_ERROR:
        default:
            return -TOKEN_WORD;         /* just in case */
        }

    case LEX_CLASS_SPECIAL:
        if (HAS_LEX_FLAG(flags, LEX_SPECIAL_AT) && *cp != '<')
            return TOKEN_EMAIL;
    next_ls:
        switch (GET_LEX_VALUE(*cp)) {

        case LEX_SPECIAL_AT:
            return -TOKEN_EMAIL;

        case LEX_SPECIAL_PERCENT:       /* %filename */
            cp = scan_state->end;
            if (*cp == '"') {
                cp = Scan_Quote_Push_Mold(mo, cp, scan_state);
                if (!cp) return -TOKEN_FILE;
                scan_state->end = cp;
                return TOKEN_FILE;
            }
            while (*cp == '/') {        /* deal with path delimiter */
                cp++;
                while (IS_LEX_NOT_DELIMIT(*cp)) cp++;
            }
            scan_state->end = cp;
            return TOKEN_FILE;

        case LEX_SPECIAL_COLON:         /* :word :12 (time) */
            if (IS_LEX_NUMBER(cp[1])) return TOKEN_TIME;
            if (ONLY_LEX_FLAG(flags, LEX_SPECIAL_WORD))
                return TOKEN_GET; /* common case */
            if (cp[1] == '\'') return -TOKEN_WORD;
            // Various special cases of < << <> >> > >= <=
            if (cp[1] == '<' || cp[1] == '>') {
                cp++;
                if (cp[1] == '<' || cp[1] == '>' || cp[1] == '=') cp++;
                if (!IS_LEX_DELIMIT(cp[1])) return -TOKEN_GET;
                scan_state->end = cp+1;
                return TOKEN_GET;
            }
            type = TOKEN_GET;
            cp++;                       /* skip ':' */
            goto scanword;

        case LEX_SPECIAL_APOSTROPHE:
            if (IS_LEX_NUMBER(cp[1])) return -TOKEN_LIT;        // no '2nd
            if (cp[1] == ':') return -TOKEN_LIT;                // no ':X
            if (
                cp[1] == '|'
                && (IS_LEX_DELIMIT(cp[2]) || IS_LEX_ANY_SPACE(cp[2]))
            ) {
                return TOKEN_LIT_BAR; // '| is a LIT-BAR!, '|foo is LIT-WORD!
            }
            if (ONLY_LEX_FLAG(flags, LEX_SPECIAL_WORD))
                return TOKEN_LIT; /* common case */
            if (!IS_LEX_WORD(cp[1])) {
                // Various special cases of < << <> >> > >= <=
                if ((cp[1] == '-' || cp[1] == '+') && IS_LEX_NUMBER(cp[2]))
                    return -TOKEN_WORD;
                if (cp[1] == '<' || cp[1] == '>') {
                    cp++;
                    if (cp[1] == '<' || cp[1] == '>' || cp[1] == '=') cp++;
                    if (!IS_LEX_DELIMIT(cp[1])) return -TOKEN_LIT;
                    scan_state->end = cp+1;
                    return TOKEN_LIT;
                }
            }
            if (cp[1] == '\'') return -TOKEN_WORD;
            type = TOKEN_LIT;
            goto scanword;

        case LEX_SPECIAL_COMMA:         /* ,123 */
        case LEX_SPECIAL_PERIOD:        /* .123 .123.456.789 */
            SET_LEX_FLAG(flags, (GET_LEX_VALUE(*cp)));
            if (IS_LEX_NUMBER(cp[1])) goto num;
            if (GET_LEX_VALUE(*cp) != LEX_SPECIAL_PERIOD) return -TOKEN_WORD;
            type = TOKEN_WORD;
            goto scanword;

        case LEX_SPECIAL_GREATER:
            if (IS_LEX_DELIMIT(cp[1])) return TOKEN_WORD;
            if (cp[1] == '>') {
                if (IS_LEX_DELIMIT(cp[2])) return TOKEN_WORD;
                return -TOKEN_WORD;
            }
        case LEX_SPECIAL_LESSER:
            if (IS_LEX_ANY_SPACE(cp[1]) || cp[1] == ']' || cp[1] == 0)
                return TOKEN_WORD;  // changed for </tag>
            if ((cp[0] == '<' && cp[1] == '<') || cp[1] == '=' || cp[1] == '>') {
                if (IS_LEX_DELIMIT(cp[2])) return TOKEN_WORD;
                return -TOKEN_WORD;
            }
            if (GET_LEX_VALUE(*cp) == LEX_SPECIAL_GREATER) return -TOKEN_WORD;
            cp = Skip_Tag(cp);
            if (!cp) return -TOKEN_TAG;
            scan_state->end = cp;
            return TOKEN_TAG;

        case LEX_SPECIAL_PLUS:          /* +123 +123.45 +$123 */
        case LEX_SPECIAL_MINUS:         /* -123 -123.45 -$123 */
            if (HAS_LEX_FLAG(flags, LEX_SPECIAL_AT)) return TOKEN_EMAIL;
            if (HAS_LEX_FLAG(flags, LEX_SPECIAL_DOLLAR)) return TOKEN_MONEY;
            if (HAS_LEX_FLAG(flags, LEX_SPECIAL_COLON)) {
                cp = Skip_To_Byte(cp, scan_state->end, ':');
                if (cp && (cp+1) != scan_state->end)
                    return TOKEN_TIME; // 12:34
                cp = scan_state->begin;
                if (cp[1] == ':') {     // +: -:
                    type = TOKEN_WORD;
                    goto scanword;
                }
            }
            cp++;
            if (IS_LEX_NUMBER(*cp)) goto num;
            if (IS_LEX_SPECIAL(*cp)) {
                if ((GET_LEX_VALUE(*cp)) >= LEX_SPECIAL_PERIOD) goto next_ls;
                if (*cp == '+' || *cp == '-') {
                    type = TOKEN_WORD;
                    goto scanword;
                }
                return -TOKEN_WORD;
            }
            type = TOKEN_WORD;
            goto scanword;

        case LEX_SPECIAL_BAR:
            //
            // `|` standalone should become a BAR!, so if followed by a
            // delimiter or space.  However `|a|` and `a|b` are left as
            // legal words (at least for the time being).
            //
            if (IS_LEX_DELIMIT(cp[1]) || IS_LEX_ANY_SPACE(cp[1]))
                return TOKEN_BAR;
            type = TOKEN_WORD;
            goto scanword;

        case LEX_SPECIAL_POUND:
        pound:
            cp++;
            if (*cp == '[') {
                scan_state->end = ++cp;
                return TOKEN_CONSTRUCT;
            }
            if (*cp == '"') { /* CHAR #"C" */
                REBUNI dummy;
                cp++;
                cp = Scan_UTF8_Char_Escapable(&dummy, cp);
                if (cp && *cp == '"') {
                    scan_state->end = cp + 1;
                    return TOKEN_CHAR;
                }
                // try to recover at next new line...
                cp = scan_state->begin + 1;
                while (!ANY_CR_LF_END(*cp)) cp++;
                scan_state->end = cp;
                return -TOKEN_CHAR;
            }
            if (*cp == '{') { /* BINARY #{12343132023902902302938290382} */
                scan_state->end = scan_state->begin;  /* save start */
                scan_state->begin = cp;
                cp = Scan_Quote_Push_Mold(mo, cp, scan_state);
                scan_state->begin = scan_state->end;  /* restore start */
                if (cp) {
                    scan_state->end = cp;
                    return TOKEN_BINARY;
                }
                // try to recover at next new line...
                cp = (scan_state->begin) + 1;
                while (!ANY_CR_LF_END(*cp)) cp++;
                scan_state->end = cp;
                return -TOKEN_BINARY;
            }
            if (cp-1 == scan_state->begin) return TOKEN_ISSUE;
            else return -TOKEN_INTEGER;

        case LEX_SPECIAL_DOLLAR:
            if (HAS_LEX_FLAG(flags, LEX_SPECIAL_AT)) return TOKEN_EMAIL;
            return TOKEN_MONEY;

        default:
            return -TOKEN_WORD;
        }

    case LEX_CLASS_WORD:
        if (ONLY_LEX_FLAG(flags, LEX_SPECIAL_WORD)) return TOKEN_WORD;
        type = TOKEN_WORD;
        goto scanword;

    case LEX_CLASS_NUMBER:      /* order of tests is important */
    num:
        if (!flags) return TOKEN_INTEGER;       /* simple integer */
        if (HAS_LEX_FLAG(flags, LEX_SPECIAL_AT)) return TOKEN_EMAIL;
        if (HAS_LEX_FLAG(flags, LEX_SPECIAL_POUND)) {
            if (cp == scan_state->begin) { // no +2 +16 +64 allowed
                if (
                    (
                        cp[0] == '6'
                        && cp[1] == '4'
                        && cp[2] == '#'
                        && cp[3] == '{'
                    ) || (
                        cp[0] == '1'
                        && cp[1] == '6'
                        && cp[2] == '#'
                        && cp[3] == '{'
                    ) // rare
                ) {
                    cp += 2;
                    goto pound;
                }
                if (cp[0] == '2' && cp[1] == '#' && cp[2] == '{') {
                    // very rare
                    cp++;
                    goto pound;
                }
            }
            return -TOKEN_INTEGER;
        }
        if (HAS_LEX_FLAG(flags, LEX_SPECIAL_COLON))
            return TOKEN_TIME; // 12:34
        if (HAS_LEX_FLAG(flags, LEX_SPECIAL_PERIOD)) {
            // 1.2 1.2.3 1,200.3 1.200,3 1.E-2
            if (Skip_To_Byte(cp, scan_state->end, 'x')) return TOKEN_PAIR;
            cp = Skip_To_Byte(cp, scan_state->end, '.');
            if (
                !HAS_LEX_FLAG(flags, LEX_SPECIAL_COMMA) // no comma in bytes
                && Skip_To_Byte(cp+1, scan_state->end, '.')
            ) {
                return TOKEN_TUPLE;
            }
            return TOKEN_DECIMAL;
        }
        if (HAS_LEX_FLAG(flags, LEX_SPECIAL_COMMA)) {
            if (Skip_To_Byte(cp, scan_state->end, 'x')) return TOKEN_PAIR;
            return TOKEN_DECIMAL;  /* 1,23 */
        }
        if (HAS_LEX_FLAG(flags, LEX_SPECIAL_POUND)) { // -#123 2#1010
            if (
                HAS_LEX_FLAGS(
                    flags,
                    ~(
                        LEX_FLAG(LEX_SPECIAL_POUND)
                        | LEX_FLAG(LEX_SPECIAL_PERIOD)
                        | LEX_FLAG(LEX_SPECIAL_APOSTROPHE)
                    )
                )
            ) {
                return -TOKEN_INTEGER;
            }
            if (HAS_LEX_FLAG(flags, LEX_SPECIAL_PERIOD)) return TOKEN_TUPLE;
            return TOKEN_INTEGER;
        }
        /* Note: cannot detect dates of the form 1/2/1998 because they
        ** may appear within a path, where they are not actually dates!
        ** Special parsing is required at the next level up. */
        for (;cp != scan_state->end; cp++) {
            // what do we hit first? 1-AUG-97 or 123E-4
            if (*cp == '-') return TOKEN_DATE;      /* 1-2-97 1-jan-97 */
            if (*cp == 'x' || *cp == 'X') return TOKEN_PAIR; // 320x200
            if (*cp == 'E' || *cp == 'e') {
                if (Skip_To_Byte(cp, scan_state->end, 'x')) return TOKEN_PAIR;
                return TOKEN_DECIMAL; /* 123E4 */
            }
            if (*cp == '%') return TOKEN_PERCENT;
        }
        /*cp = scan_state->begin;*/
        if (HAS_LEX_FLAG(flags, LEX_SPECIAL_APOSTROPHE))
            return TOKEN_INTEGER; // 1'200
        return -TOKEN_INTEGER;

    default:
        return -TOKEN_WORD;
    }

    DEAD_END;

scanword:
    if (HAS_LEX_FLAG(flags, LEX_SPECIAL_COLON)) { // word:  url:words
        if (type != TOKEN_WORD) {
            // only valid with WORD (not set or lit)
            return type;
        }
        // This Skip_To_Byte always returns a pointer (always a ':')
        cp = Skip_To_Byte(cp, scan_state->end, ':');
        if (cp[1] != '/' && Lex_Map[(REBYTE)cp[1]] < LEX_SPECIAL) {
            // a valid delimited word SET?
            if (
                HAS_LEX_FLAGS(
                    flags, ~LEX_FLAG(LEX_SPECIAL_COLON) & LEX_WORD_FLAGS
                )
            ) {
                return -TOKEN_WORD;
            }
            return TOKEN_SET;
        }
        cp = scan_state->end;   /* then, must be a URL */
        while (*cp == '/') {    /* deal with path delimiter */
            cp++;
            while (IS_LEX_NOT_DELIMIT(*cp) || *cp == '/') cp++;
        }
        scan_state->end = cp;
        return TOKEN_URL;
    }
    if (HAS_LEX_FLAG(flags, LEX_SPECIAL_AT)) return TOKEN_EMAIL;
    if (HAS_LEX_FLAG(flags, LEX_SPECIAL_DOLLAR)) return TOKEN_MONEY;
    if (HAS_LEX_FLAGS(flags, LEX_WORD_FLAGS)) {
        // has chars not allowed in word (eg % \ )
        return -type;
    }
    if (HAS_LEX_FLAG(flags, LEX_SPECIAL_LESSER)) {
        // Allow word<tag> and word</tag> but not word< word<= word<> etc.
        cp = Skip_To_Byte(cp, scan_state->end, '<');
        if (cp[1] == '<' || cp[1] == '>' || cp[1] == '=' ||
            IS_LEX_SPACE(cp[1]) || (cp[1] != '/' && IS_LEX_DELIMIT(cp[1])))
            return -type;
        scan_state->end = cp;
    } else if (HAS_LEX_FLAG(flags, LEX_SPECIAL_GREATER)) return -type;
    return type;
}


//
//  Init_Scan_State: C
// 
// Initialize a scanner state structure.  Set the standard
// scan pointers and the limit pointer.
//
static void Init_Scan_State(SCAN_STATE *scan_state, const REBYTE *cp, REBCNT limit)
{
    // Not all scans finish successfully, and if they're stopped by an error
    // may leave lingering data in the emit buffer.  This cleans it upon
    // every new scan initialization.
    // !!! Is it too slow to have all scans be in a TRAP that does this?
    RESET_ARRAY(BUF_EMIT);

    scan_state->head_line = scan_state->begin = scan_state->end = cp;
    scan_state->limit = cp + limit;
    scan_state->line_count = 1;
    scan_state->opts = 0;
    scan_state->errors = 0;
}


//
//  Scan_Head: C
// 
// Search text for a REBOL header.  It is distinguished as
// the word REBOL followed by a '[' (they can be separated
// only by lines and comments).  There can be nothing on the
// line before the header.  Also, if a '[' preceedes the
// header, then note its position (for embedded code).
// The scan_state begin pointer is updated to point to the header block.
// Keep track of line-count.
// 
// Returns:
//     0 if no header,
//     1 if header,
//    -1 if embedded header (inside []).
// 
// The scan_state structure is updated to point to the
// beginning of the source text.
//
static REBINT Scan_Head(SCAN_STATE *scan_state)
{
    const REBYTE *rp = 0;   /* pts to the REBOL word */
    const REBYTE *bp = 0;   /* pts to optional [ just before REBOL */
    const REBYTE *cp = scan_state->begin;
    REBCNT count = scan_state->line_count;

    while (TRUE) {
        while (IS_LEX_SPACE(*cp)) cp++; /* skip white space */
        switch (*cp) {
        case '[':
            if (rp) {
                scan_state->begin = ++cp; //(bp ? bp : cp);
                scan_state->line_count = count;
                return (bp ? -1 : 1);
            }
            bp = cp++;
            break;
        case 'R':
        case 'r':
            if (Match_Bytes(cp, cb_cast(Str_REBOL))) {
                rp = cp;
                cp += 5;
                break;
            }
            cp++;
            bp = 0; /* prior '[' was a red herring */
            /* fall thru... */
        case ';':
            goto skipline;
        case 0:
            return 0;
        default:    /* everything else... */
            if (!ANY_CR_LF_END(*cp)) rp = bp = 0;
        skipline:
            while (!ANY_CR_LF_END(*cp)) cp++;
            if (*cp == CR && cp[1] == LF) cp++;
            if (*cp) cp++;
            count++;
            break;
        }
    }
}


static REBARR *Scan_Full_Block(SCAN_STATE *scan_state, REBYTE mode_char);

//
//  Scan_Block: C
// 
// Scan a block (or group) and return it.
// Sub scanners may return bad by setting value type to zero.
//
static REBARR *Scan_Block(SCAN_STATE *scan_state, REBYTE mode_char)
{
    REBINT token;
    REBCNT len;
    const REBYTE *bp;
    const REBYTE *ep;
    REBVAL *value = 0;
    REBARR *emitbuf = BUF_EMIT;
    REBARR *block;
    REBCNT begin = ARR_LEN(emitbuf);   // starting point in block buffer
    REBOOL line = FALSE;
#ifdef COMP_LINES
    REBINT linenum;
#endif
    REBCNT start = scan_state->line_count;
    const REBYTE *start_line = scan_state->head_line;
    // just_once for load/next see Load_Script for more info.
    REBOOL just_once = GET_FLAG(scan_state->opts, SCAN_NEXT);
    REB_MOLD mo;

    CLEARS(&mo);

    if (C_STACK_OVERFLOWING(&token)) Trap_Stack_Overflow();

    if (just_once)
        CLR_FLAG(scan_state->opts, SCAN_NEXT); // no deeper

    while (
#ifdef COMP_LINES
        linenum=scan_state->line_count,
#endif
        Drop_Mold_If_Pushed(&mo),
        ((token = Locate_Token_May_Push_Mold(&mo, scan_state)) != TOKEN_END)
    ) {

        bp = scan_state->begin;
        ep = scan_state->end;
        len = (REBCNT)(ep - bp);

        if (token < 0) {    // Check for error tokens
            token = -token;
            scan_state->begin = scan_state->end; // skip malformed token
            goto syntax_error;
        }

        // Is output block buffer large enough?
        if (token >= TOKEN_WORD && SER_FULL(ARR_SERIES(emitbuf)))
            Extend_Series(ARR_SERIES(emitbuf), 1024);

        value = ARR_TAIL(emitbuf);
        SET_END(value);

        // If in a path, handle start of path /word or word//word cases:
        if (mode_char == '/' && *bp == '/') {
            SET_NONE(value);
            SET_ARRAY_LEN(emitbuf, ARR_LEN(emitbuf) + 1);
            scan_state->begin = bp + 1;
            continue;
        }

        // Check for new path: /word or word/word:
        if (
            (
                token == TOKEN_PATH
                || (
                    (
                        token == TOKEN_WORD
                        || token == TOKEN_LIT
                        || token == TOKEN_GET
                    )
                    && *ep == '/'
                )
            )
            && mode_char != '/'
        ) {
            block = Scan_Block(scan_state, '/');  // (could realloc emitbuf)
            value = ARR_TAIL(emitbuf);
            if (token == TOKEN_LIT) {
                token = REB_LIT_PATH;
                VAL_RESET_HEADER(ARR_HEAD(block), REB_WORD);
                assert(IS_WORD_UNBOUND(ARR_HEAD(block)));
            }
            else if (IS_GET_WORD(ARR_HEAD(block))) {
                if (*scan_state->end == ':') goto syntax_error;
                token = REB_GET_PATH;
                VAL_RESET_HEADER(ARR_HEAD(block), REB_WORD);
                assert(IS_WORD_UNBOUND(ARR_HEAD(block)));
            }
            else {
                if (*scan_state->end == ':') {
                    token = REB_SET_PATH;
                    scan_state->begin = ++(scan_state->end);
                } else token = REB_PATH;
            }
            VAL_RESET_HEADER(value, cast(enum Reb_Kind, token));
            VAL_ARRAY(value) = block;
            VAL_INDEX(value) = 0;
            token = TOKEN_PATH;
        } else {
            scan_state->begin = scan_state->end; // accept token
        }

        // Process each lexical token appropriately:
        switch (token) {  // (idea is that compiler selects computed branch)

        case TOKEN_NEWLINE:
#ifdef TEST_SCAN
            Wait_User("next...");
#endif
            line = TRUE;
            scan_state->head_line = ep;
            continue;

        case TOKEN_BAR:
            SET_BAR(value);
            ++bp;
            break;

        case TOKEN_LIT_BAR:
            SET_LIT_BAR(value);
            ++bp;
            break;

        case TOKEN_LIT:
        case TOKEN_GET:
            if (ep[-1] == ':') {
                if (len == 1 || mode_char != '/') goto syntax_error;
                len--, scan_state->end--;
            }
            bp++;
        case TOKEN_SET:
            len--;
            if (mode_char == '/' && token == TOKEN_SET) {
                token = TOKEN_WORD; // will be a PATH_SET
                scan_state->end--;  // put ':' back on end but not beginning
            }
        case TOKEN_WORD:
            if (len == 0) {bp--; goto syntax_error;}
            Val_Init_Word(
                value,
                KIND_OF_WORD_FROM_TOKEN(token),
                Make_Word(bp, len)
            );
            break;

        case TOKEN_REFINE:
            Val_Init_Word(value, REB_REFINEMENT, Make_Word(bp + 1, len - 1));
            break;

        case TOKEN_ISSUE:
            if (len == 1) {
                if (bp[1] == '(') {
                    token = TOKEN_CONSTRUCT;
                    goto syntax_error;
                }
                SET_NONE(value);  // A single # means NONE
            }
            else {
                REBSYM sym = Scan_Issue(bp + 1, len - 1);
                if (sym == SYM_0)
                    goto syntax_error;
                Val_Init_Word(value, REB_ISSUE, sym);
            }
            break;

        case TOKEN_BLOCK_BEGIN:
        case TOKEN_PAREN_BEGIN:
            block = Scan_Block(
                scan_state, (token == TOKEN_BLOCK_BEGIN) ? ']' : ')'
            );
            // (above line could have realloced emitbuf)
            ep = scan_state->end;
            value = ARR_TAIL(emitbuf);
            if (scan_state->errors) {
                *value = *ARR_LAST(block); // Copy the error
                SET_ARRAY_LEN(emitbuf, ARR_LEN(emitbuf) + 1);
                goto exit_block;
            }
            Val_Init_Array(
                value,
                (token == TOKEN_BLOCK_BEGIN) ? REB_BLOCK : REB_GROUP,
                block
            );
            break;

        case TOKEN_PATH:
            break;

        case TOKEN_BLOCK_END:
            if (!mode_char) { mode_char = '['; goto extra_error; }
            else if (mode_char != ']') goto missing_error;
            else goto exit_block;

        case TOKEN_PAREN_END:
            if (!mode_char) { mode_char = '('; goto extra_error; }
            else if (mode_char != ')') goto missing_error;
            else goto exit_block;

        case TOKEN_INTEGER:     // or start of DATE
            if (*ep != '/' || mode_char == '/') {
                VAL_RESET_HEADER(value, REB_INTEGER);
                if (!Scan_Integer(&VAL_INT64(value), bp, len))
                    goto syntax_error;
            }
            else {              // A / and not in block
                token = TOKEN_DATE;
                while (*ep == '/' || IS_LEX_NOT_DELIMIT(*ep)) ep++;
                scan_state->begin = ep;
                len = (REBCNT)(ep - bp);
                if (ep != Scan_Date(bp, len, value)) goto syntax_error;
            }
            break;

        case TOKEN_DECIMAL:
        case TOKEN_PERCENT:
            // Do not allow 1.2/abc:
            VAL_RESET_HEADER(value, REB_DECIMAL);
            if (
                *ep == '/'
                || !Scan_Decimal(&VAL_DECIMAL(value), bp, len, FALSE)
            ) {
                goto syntax_error;
            }
            if (bp[len-1] == '%') {
                VAL_RESET_HEADER(value, REB_PERCENT);
                VAL_DECIMAL(value) /= 100.0;
            }
            break;

        case TOKEN_MONEY:
            // Do not allow $1/$2:
            if (*ep == '/') {ep++; goto syntax_error;}
            if (!Scan_Money(bp, len, value)) goto syntax_error;
            break;

        case TOKEN_TIME:
            if (bp[len-1] == ':' && mode_char == '/') { // could be path/10: set
                VAL_RESET_HEADER(value, REB_INTEGER);
                if (!Scan_Integer(&VAL_INT64(value), bp, len - 1))
                    goto syntax_error;
                scan_state->end--;  // put ':' back on end but not beginning
                break;
            }
            if (ep != Scan_Time(bp, len, value)) goto syntax_error;
            break;

        case TOKEN_DATE:
            while (*ep == '/' && mode_char != '/') {  // Is it a date/time?
                ep++;
                while (IS_LEX_NOT_DELIMIT(*ep)) ep++;
                len = (REBCNT)(ep - bp);
                if (len > 50) {
                    // prevent infinite loop, should never be longer than this
                    break;
                }
                scan_state->begin = ep;  // End point extended to cover time
            }
            if (ep != Scan_Date(bp, len, value)) goto syntax_error;
            break;

        case TOKEN_CHAR:
            bp += 2; // skip #"
            if (!Scan_UTF8_Char_Escapable(&VAL_CHAR(value), bp))
                goto syntax_error;
            VAL_RESET_HEADER(value, REB_CHAR);
            break;

        case TOKEN_STRING:
            // During scan above, string was stored in UNI_BUF (with Uni width)
            Val_Init_String(value, Pop_Molded_String(&mo));
            break;

        case TOKEN_BINARY:
            Scan_Binary(bp, len, value);
            break;

        case TOKEN_PAIR:
            Scan_Pair(bp, len, value);
            break;

        case TOKEN_TUPLE:
            if (!Scan_Tuple(bp, len, value)) goto syntax_error;
            break;

        case TOKEN_FILE:
            Scan_File(bp, len, value);
            break;

        case TOKEN_EMAIL:
            Scan_Email(bp, len, value);
            break;

        case TOKEN_URL:
            Scan_URL(bp, len, value);
            break;

        case TOKEN_TAG:
            Scan_Any(bp+1, len-2, value, REB_TAG);
            break;

        case TOKEN_CONSTRUCT:
            block = Scan_Full_Block(scan_state, ']');
            value = ARR_TAIL(emitbuf);
            SET_ARRAY_LEN(emitbuf, ARR_LEN(emitbuf) + 1); // Protect from GC
            Bind_Values_All_Deep(ARR_HEAD(block), Lib_Context);
            if (!Construct_Value(value, block)) {
                if (IS_END(value)) Val_Init_Block(value, block);
                fail (Error(RE_MALCONSTRUCT, value));
            }
            SET_ARRAY_LEN(emitbuf, ARR_LEN(emitbuf) - 1); // Unprotect
            break;

        case TOKEN_END:
            continue;

        default:
            SET_NONE(value);
        }

        if (line) {
            line = FALSE;
            SET_VAL_FLAG(value, VALUE_FLAG_LINE);
        }

#ifdef TEST_SCAN
        Print((REBYTE*)"%s - %s", Token_Names[token], Use_Buf(bp,ep));
        if (VAL_TYPE(value) >= REB_STRING && VAL_TYPE(value) <= REB_URL)
            Print_Str(VAL_BIN(value));
        //Wait_User(0);
#endif

#ifdef COMP_LINES
        VAL_LINE(value)=linenum;
        VAL_FLAGS(value)|=FLAGS_LINE;
#endif
        if (!IS_END(value))
            SET_ARRAY_LEN(emitbuf, ARR_LEN(emitbuf) + 1);
        else {
            REBCTX *error;
        syntax_error:
            error = Error_Bad_Scan(
                RE_INVALID,
                scan_state,
                cast(REBCNT, token),
                bp,
                cast(REBCNT, ep - bp)
            );
            if (GET_FLAG(scan_state->opts, SCAN_RELAX)) {
                Val_Init_Error(ARR_TAIL(emitbuf), error);
                SET_ARRAY_LEN(emitbuf, ARR_LEN(emitbuf) + 1);
                goto exit_block;
            }
            fail (error);

        missing_error:
            scan_state->line_count = start; // where block started
            scan_state->head_line = start_line;
        extra_error: {
                REBYTE tmp_buf[4];  // Temporary error string
                tmp_buf[0] = mode_char;
                tmp_buf[1] = 0;
                error = Error_Bad_Scan(
                    RE_MISSING,
                    scan_state,
                    cast(REBCNT, token),
                    tmp_buf,
                    1
                );
                if (GET_FLAG(scan_state->opts, SCAN_RELAX)) {
                    Val_Init_Error(ARR_TAIL(emitbuf), error);
                    SET_ARRAY_LEN(emitbuf, ARR_LEN(emitbuf) + 1);
                    goto exit_block;
                }
                fail (error);
            }
        }

        // Check for end of path:
        if (mode_char == '/') {
            if (*ep == '/') {
                ep++;
                scan_state->begin = ep;  // skip next /
                if (*ep != '(' && IS_LEX_DELIMIT(*ep)) {
                    token = TOKEN_PATH;
                    goto syntax_error;
                }
            }
            else goto exit_block;
        }

        // Added for load/next
        if (GET_FLAG(scan_state->opts, SCAN_ONLY) || just_once)
            goto exit_block;
    }

    if (mode_char == ']' || mode_char == ')') goto missing_error;

exit_block:
    Drop_Mold_If_Pushed(&mo);

    if (line && value) SET_VAL_FLAG(value, VALUE_FLAG_LINE);

#ifdef TEST_SCAN
    Print((REBYTE*)"block of %d values ", emitbuf->tail - begin);
#endif

    len = ARR_LEN(emitbuf);
    block = Copy_Values_Len_Shallow(ARR_AT(emitbuf, begin), len - begin);
    ASSERT_SERIES_TERM(ARR_SERIES(block));

    SET_ARRAY_LEN(emitbuf, begin);

    // All scanned code is expected to be managed by the GC (because walking
    // the tree after constructing it to add the "manage GC" bit would be
    // too expensive, and we don't load source and free it manually anyway)
    //
    MANAGE_ARRAY(block);
    return block;
}


//
//  Scan_Full_Block: C
// 
// Simple variation of scan_block to avoid problem with
// construct of aggregate values.
//
static REBARR *Scan_Full_Block(SCAN_STATE *scan_state, REBYTE mode_char)
{
    REBOOL only = GET_FLAG(scan_state->opts, SCAN_ONLY);
    REBARR *array;
    CLR_FLAG(scan_state->opts, SCAN_ONLY);
    array = Scan_Block(scan_state, mode_char);
    if (only) SET_FLAG(scan_state->opts, SCAN_ONLY);
    return array;
}


//
//  Scan_Source: C
// 
// Scan source code. Scan state initialized. No header required.
//
REBARR *Scan_Source(const REBYTE *src, REBCNT len)
{
    SCAN_STATE scan_state;
    Init_Scan_State(&scan_state, src, len);
    return Scan_Block(&scan_state, 0);
}


//
//  Scan_Header: C
// 
// Scan for header, return its offset if found or -1 if not.
//
REBINT Scan_Header(const REBYTE *src, REBCNT len)
{
    SCAN_STATE scan_state;
    const REBYTE *cp;
    REBINT result;

    // Must be UTF8 byte-stream:
    Init_Scan_State(&scan_state, src, len);
    result = Scan_Head(&scan_state);
    if (!result) return -1;

    cp = scan_state.begin-2;
    // Backup to start of it:
    if (result > 0) { // normal header found
        while (cp != src && *cp != 'r' && *cp != 'R') cp--;
    } else {
        while (cp != src && *cp != '[') cp--;
    }
    return (REBINT)(cp - src);
}


//
//  Init_Scanner: C
//
void Init_Scanner(void)
{
    Set_Root_Series(TASK_BUF_EMIT, ARR_SERIES(Make_Array(511)));
    Set_Root_Series(TASK_BUF_UTF8, Make_Unicode(1020));
}


//
//  Shutdown_Scanner: C
//
void Shutdown_Scanner(void)
{
    // Note: Emit and UTF8 buffers freed by task root set
}


//
//  transcode: native [
//  
//  {Translates UTF-8 binary source to values. Returns [value binary].}
//  
//      source [binary!] "Must be Unicode UTF-8 encoded"
//      /next {Translate next complete value (blocks as single value)}
//      /only "Translate only a single value (blocks dissected)"
//      /error {Do not cause errors - return error object as value in place}
//  ]
//
REBNATIVE(transcode)
{
    PARAM(1, source);
    REFINE(2, next);
    REFINE(3, only);
    REFINE(4, relax);

    SCAN_STATE scan_state;

    assert(IS_BINARY(ARG(source)));

    Init_Scan_State(
        &scan_state, VAL_BIN_AT(ARG(source)), VAL_LEN_AT(ARG(source))
    );

    if (REF(next)) SET_FLAG(scan_state.opts, SCAN_NEXT);
    if (REF(only)) SET_FLAG(scan_state.opts, SCAN_ONLY);
    if (REF(relax)) SET_FLAG(scan_state.opts, SCAN_RELAX);

    // The scanner always returns an "array" series.  So set the result
    // to a BLOCK! of the results.
    //
    // If the source data bytes are "1" then it will be the block [1]
    // if the source data is "[1]" then it will be the block [[1]]

    Val_Init_Block(D_OUT, Scan_Block(&scan_state, 0));

    // Add a value to the tail of the result, representing the input
    // with position advanced past the content consumed by the scan.
    // (Returning a length 2 block is how TRANSCODE does a "multiple
    // return value, but #1916 discusses a possible "revamp" of this.)

    VAL_INDEX(ARG(source)) = scan_state.end - VAL_BIN(ARG(source));
    Append_Value(VAL_ARRAY(D_OUT), ARG(source));

    return R_OUT;
}


//
//  Scan_Word: C
// 
// Scan word chars and make word symbol for it.
// This method gets exactly the same results as scanner.
// Returns symbol number, or zero for errors.
//
REBCNT Scan_Word(const REBYTE *cp, REBCNT len)
{
    SCAN_STATE scan_state;
    REBSYM sym = 0;
    REB_MOLD mo;
    CLEARS(&mo);

    Init_Scan_State(&scan_state, cp, len);

    if (TOKEN_WORD == Locate_Token_May_Push_Mold(&mo, &scan_state))
        sym = Make_Word(cp, len);

    Drop_Mold_If_Pushed(&mo);
    return sym;
}


//
//  Scan_Issue: C
// 
// Scan an issue word, allowing special characters.
//
REBCNT Scan_Issue(const REBYTE *cp, REBCNT len)
{
    const REBYTE *bp;
    REBCNT l = len;
    REBCNT c;

    if (len == 0) return SYM_0; // will trigger error

    while (IS_LEX_SPACE(*cp)) cp++; /* skip white space */

    bp = cp;

    while (l > 0) {
        switch (GET_LEX_CLASS(*bp)) {

        case LEX_CLASS_DELIMIT:
            return SYM_0; // will trigger error

        case LEX_CLASS_SPECIAL:     /* Flag all but first special char: */
            c = GET_LEX_VALUE(*bp);
            if (!(LEX_SPECIAL_APOSTROPHE == c
                || LEX_SPECIAL_COMMA  == c
                || LEX_SPECIAL_PERIOD == c
                || LEX_SPECIAL_PLUS   == c
                || LEX_SPECIAL_MINUS  == c
                || LEX_SPECIAL_TILDE  == c
            )) {
                return SYM_0; // will trigger error
            }
            // fallthrough
        case LEX_CLASS_WORD:
        case LEX_CLASS_NUMBER:
            bp++;
            l--;
            break;
        }
    }

    return Make_Word(cp, len);
}
