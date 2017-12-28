//
//  File: %l-scan.c
//  Summary: "lexical analyzer for source to binary translation"
//  Section: lexical
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
// Rebol's lexical scanner was implemented as hand-coded C, as opposed to
// using a more formal grammar and generator.  This makes the behavior hard
// to formalize, though some attempts have been made to do so:
//
// http://rgchris.github.io/Rebol-Notation/
//
// Because Red is implemented using Rebol, it has a more abstract definition
// in the sense that it uses PARSE rules:
//
// https://github.com/red/red/blob/master/lexer.r
//
// It would likely be desirable to bring more formalism and generativeness
// to Rebol's scanner; though the current method of implementation was
// ostensibly chosen for performance.
//

#include "sys-core.h"


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
    /* 5F _   */    LEX_SPECIAL|LEX_SPECIAL_BLANK,

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
    LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
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
        *out = '\t'; // tab character
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
// Scan a quoted string, handling all the escape characters.  e.g. an input
// stream might have "a^(1234)b" and need to turn "^(1234)" into the right
// UTF-8 bytes for that codepoint in the string.
//
// !!! In R3-Alpha the mold buffer held 16-bit codepoints.  Ren-C uses UTF-8
// everywhere, and so molding is naturally done into a byte buffer.  This is
// more compatible with the fact that the incoming stream is UTF-8 bytes, so
// optimizations will be possible.  As a first try, just getting it working
// is the goal.
//
static const REBYTE *Scan_Quote_Push_Mold(
    REB_MOLD *mo,
    const REBYTE *src,
    SCAN_STATE *ss
) {
    assert(ss != NULL);

    Push_Mold(mo);

    REBUNI term = (*src == '{') ? '}' : '"'; // pick termination
    ++src;

    REBINT nest = 0;
    REBCNT lines = 0;
    while (*src != term || nest > 0) {
        REBUNI chr = *src;

        switch (chr) {

        case 0:
            TERM_BIN(mo->series);
            return NULL; // Scan_state shows error location.

        case '^':
            if ((src = Scan_UTF8_Char_Escapable(&chr, src)) == NULL) {
                TERM_BIN(mo->series);
                return NULL;
            }
            --src;
            break;

        case '{':
            if (term != '"')
                ++nest;
            break;

        case '}':
            if (term != '"' && nest > 0)
                --nest;
            break;

        case CR:
            if (src[1] == LF) src++;
            // fall thru
        case LF:
            if (term == '"') {
                TERM_BIN(mo->series);
                return NULL;
            }
            lines++;
            chr = LF;
            break;

        default:
            if (chr >= 0x80) {
                if ((src = Back_Scan_UTF8_Char(&chr, src, NULL)) == NULL) {
                    TERM_BIN(mo->series);
                    return NULL;
                }
            }
        }

        src++;

        // 4 bytes maximum for UTF-8 encoded character (6 is a lie)
        //
        // https://stackoverflow.com/a/9533324/211160
        //
        if (SER_LEN(mo->series) + 4 >= SER_REST(mo->series)) // incl term
            Extend_Series(mo->series, 4);

        REBCNT encoded_len = Encode_UTF8_Char(BIN_TAIL(mo->series), chr);
        SET_SERIES_LEN(mo->series, SER_LEN(mo->series) + encoded_len);
    }

    src++; // Skip ending quote or brace.

    ss->line += lines;

    TERM_BIN(mo->series);
    return src;
}


//
//  Scan_Item_Push_Mold: C
//
// Scan as UTF8 an item like a file.  Handles *some* forms of escaping, which
// may not be a great idea (see notes below on how URL! moved away from that)
//
// Returns continuation point or zero for error.  Puts result into the
// temporary mold buffer as UTF-8.
//
// !!! See notes on Scan_Quote_Push_Mold about the inefficiency of this
// interim time of changing the mold buffer from 16-bit codepoints to UTF-8
//
const REBYTE *Scan_Item_Push_Mold(
    REB_MOLD *mo,
    const REBYTE *bp,
    const REBYTE *ep,
    REBYTE opt_term, // '\0' if file like %foo - '"' if file like %"foo bar"
    const REBYTE *opt_invalids
){
    assert(opt_term < 128); // method below doesn't search for high chars

    Push_Mold(mo);

    while (bp < ep && *bp != opt_term) {
        REBUNI c = *bp;

        if (c == '\0')
            break; // End of stream

        if ((opt_term == '\0') && IS_WHITE(c))
            break; // Unless terminator like '"' %"...", any whitespace ends

        if (c < ' ')
            return NULL; // Ctrl characters not valid in filenames, fail

        // !!! The branches below do things like "forces %\foo\bar to become
        // %/foo/bar".  But it may be that this kind of lossy scanning is a
        // poor idea, and it's better to preserve what the user entered then
        // have FILE-TO-LOCAL complain it's malformed when turning to a
        // STRING!--or be overridden explicitly to be lax and tolerate it.
        //
        // (URL! has already come under scrutiny for these kinds of automatic
        // translations that affect round-trip copy and paste, and it seems
        // applicable to FILE! too.)
        //
        if (c == '\\') {
            c = '/';
        }
        else if (c == '%') { // Accept %xx encoded char:
            const REBOOL unicode = FALSE;
            if (!Scan_Hex2(&c, bp + 1, unicode))
                return NULL;
            bp += 2;
        }
        else if (c == '^') { // Accept ^X encoded char:
            if (bp + 1 == ep)
                return NULL; // error if nothing follows ^
            if (NULL == (bp = Scan_UTF8_Char_Escapable(&c, bp)))
                return NULL;
            if (opt_term == '\0' && IS_WHITE(c))
                break;
            bp--;
        }
        else if (c >= 0x80) { // Accept UTF8 encoded char:
            if (NULL == (bp = Back_Scan_UTF8_Char(&c, bp, 0)))
                return NULL;
        }
        else if (opt_invalids && strchr(cs_cast(opt_invalids), c)) {
            //
            // Is char as literal valid? (e.g. () [] etc.)
            // Only searches ASCII characters.
            //
            return NULL;
        }

        ++bp;

        // 4 bytes maximum for UTF-8 encoded character (6 is a lie)
        //
        // https://stackoverflow.com/a/9533324/211160
        //
        if (SER_LEN(mo->series) + 4 >= SER_REST(mo->series)) // incl term
            Extend_Series(mo->series, 4);

        REBCNT encoded_len = Encode_UTF8_Char(BIN_TAIL(mo->series), c);
        SET_SERIES_LEN(mo->series, SER_LEN(mo->series) + encoded_len);
    }

    if (*bp != '\0' && *bp == opt_term)
        ++bp;

    TERM_BIN(mo->series);

    return bp;
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
//  Update_Error_Near_For_Line: C
//
// The NEAR information in an error is typically expressed in terms of loaded
// Rebol code.  Scanner errors have historically used the NEAR not to tell you
// where the LOAD that is failing is in Rebol, but to form a string of the
// "best place" to report the textual error.
//
// While this is probably a bad overloading of NEAR, it is being made more
// clear that this is what's happening for the moment.
//
static void Update_Error_Near_For_Line(
    REBCTX *error,
    REBCNT line,
    const REBYTE *line_head
){
    // Skip indentation (don't include in the NEAR)
    //
    const REBYTE *cp = line_head;
    while (IS_LEX_SPACE(*cp))
        ++cp;

    // Find end of line to capture in error message
    //
    REBCNT len = 0;
    const REBYTE *bp = cp;
    while (!ANY_CR_LF_END(*cp)) {
        cp++;
        len++;
    }

    // Put the line count and the line's text into a string.
    //
    // !!! This should likely be separated into an integer and a string, so
    // that those processing the error don't have to parse it back out.
    //
    DECLARE_MOLD (mo);
    Push_Mold(mo);
    Append_Unencoded(mo->series, "(line ");
    Append_Int(mo->series, line);
    Append_Unencoded(mo->series, ") ");
    Append_Utf8_Utf8(mo->series, cs_cast(bp), len);

    ERROR_VARS *vars = ERR_VARS(error);
    Init_String(&vars->nearest, Pop_Molded_String(mo));
}


//
//  Error_Syntax: C
//
// Catch-all scanner error handler.  Reports the name of the token that gives
// the complaint, and gives the substring of the token's text.  Populates
// the NEAR field of the error with the "current" line number and line text,
// e.g. where the end point of the token is seen.
//
static REBCTX *Error_Syntax(SCAN_STATE *ss) {
    //
    // The scanner code has `bp` and `ep` locals which mirror ss->begin and
    // ss->end.  However, they get out of sync.  If they are updated, they
    // should be sync'd before calling here, since it's used to find the
    // range of text to report.
    //
    // !!! Would it be safer to go to ss->b and ss->e, or something similar,
    // to get almost as much brevity and not much less clarity than bp and
    // ep, while avoiding the possibility of the state getting out of sync?
    //
    assert(ss->begin != NULL && NOT(IS_POINTER_TRASH_DEBUG(ss->begin)));
    assert(ss->end != NULL && NOT(IS_POINTER_TRASH_DEBUG(ss->end)));
    assert(ss->end >= ss->begin);

    DECLARE_LOCAL (token_name);
    Init_String(
        token_name,
        Make_String_UTF8(Token_Names[ss->token])
    );

    DECLARE_LOCAL (token_text);
    Init_String(
        token_text,
        Make_Sized_String_UTF8(
            cs_cast(ss->begin), cast(REBCNT, ss->end - ss->begin)
        )
    );

    REBCTX *error = Error(RE_SCAN_INVALID, token_name, token_text, END);
    Update_Error_Near_For_Line(error, ss->line, ss->line_head);
    return error;
}


//
//  Error_Missing: C
//
// For instance, `load "( abc"`.
//
// Note: This error is useful for things like multi-line input, because it
// indicates a state which could be reconciled by adding more text.  A
// better form of this error would walk the scan state stack and be able to
// report all the unclosed terms.
//
static REBCTX *Error_Missing(SCAN_STATE *ss, char wanted) {
    DECLARE_LOCAL (expected);
    Init_String(expected, Make_Series_Codepoint(wanted));

    REBCTX *error = Error(RE_SCAN_MISSING, expected, END);
    Update_Error_Near_For_Line(error, ss->start_line, ss->start_line_head);
    return error;
}


//
//  Error_Extra: C
//
// For instance, `load "abc ]"`
//
static REBCTX *Error_Extra(SCAN_STATE *ss, char seen) {
    DECLARE_LOCAL (unexpected);
    Init_String(unexpected, Make_Series_Codepoint(seen));

    REBCTX *error = Error(RE_SCAN_EXTRA, unexpected, END);
    Update_Error_Near_For_Line(error, ss->line, ss->line_head);
    return error;
}


//
//  Error_Mismatch: C
//
// For instance, `load "( abc ]"`
//
// Note: This answer would be more useful for syntax highlighting or other
// applications if it would point out the locations of both points.  R3-Alpha
// only pointed out the location of the start token.
//
static REBCTX *Error_Mismatch(SCAN_STATE *ss, char wanted, char seen) {
    DECLARE_LOCAL (expected);
    DECLARE_LOCAL (unexpected);
    REBCTX *error = Error(
        RE_SCAN_MISMATCH,
        Init_String(expected, Make_Series_Codepoint(wanted)),
        Init_String(unexpected, Make_Series_Codepoint(seen)),
        END
    );
    Update_Error_Near_For_Line(error, ss->start_line, ss->start_line_head);
    return error;
}


//
//  Prescan_Token: C
//
// This function updates `ss->begin` to skip past leading
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
// the caller will use GET_LEX_CLASS(ss->begin[0]).
// Fingerprinting just helps accelerate further categorization.
//
static REBCNT Prescan_Token(SCAN_STATE *ss)
{
    assert(IS_POINTER_TRASH_DEBUG(ss->end)); // prescan only uses ->begin

    const REBYTE *cp = ss->begin;
    REBCNT flags = 0;

    // Skip whitespace (if any) and update the ss
    while (IS_LEX_SPACE(*cp)) cp++;
    ss->begin = cp;

    while (TRUE) {
        switch (GET_LEX_CLASS(*cp)) {

        case LEX_CLASS_DELIMIT:
            if (cp == ss->begin) {
                // Include the delimiter if it is the only character we
                // are returning in the range (leave it out otherwise)
                ss->end = cp + 1;

                // Note: We'd liked to have excluded LEX_DELIMIT_END, but
                // would require a GET_LEX_VALUE() call to know to do so.
                // Locate_Token_May_Push_Mold() does a `switch` on that,
                // so it can subtract this addition back out itself.
            }
            else
                ss->end = cp;
            return flags;

        case LEX_CLASS_SPECIAL:
            if (cp != ss->begin) {
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

    DEAD_END;
}


//
//  Locate_Token_May_Push_Mold: C
//
// Find the beginning and end character pointers for the next token in the
// scanner state.  If the scanner is being fed variadically by a list of UTF-8
// strings and REBVAL pointers, then any Rebol values encountered will be
// spliced into the array being currently gathered by pushing them to the data
// stack (as tokens can only be located in UTF-8 strings encountered).
//
// The scan state will be updated so that `ss->begin` has been moved past any
// leading whitespace that was pending in the buffer.  `ss->end` will hold the
// conclusion at a delimiter.  `ss->token` will return the calculated token.
//
// The TOKEN_XXX type returned will correspond directly to a Rebol datatype
// if it isn't an ANY-ARRAY! (e.g. TOKEN_INTEGER for INTEGER! or TOKEN_STRING
// for STRING!).  When a block or group delimiter is found it will indicate
// that, e.g. TOKEN_BLOCK_BEGIN will be returned to indicate the scanner
// should recurse... or TOKEN_GROUP_END which will signal the end of a level
// of recursion.
//
// TOKEN_END is returned if end of input is reached.
//
// !!! This should be modified to explain how paths work, once
// I can understand how paths work. :-/  --HF
//
// Newlines that should be internal to a non-ANY-ARRAY! type are included in
// the scanned range between the `begin` and `end`.  But newlines that are
// found outside of a string are returned as TOKEN_NEWLINE.  (These are used
// to set the VALUE_FLAG_LINE formatting bit on the subsequent value.)
//
// Determining the end point of token types that need escaping requires
// processing (for instance `{a^}b}` can't see the first close brace as ending
// the string).  To avoid double processing, the routine decodes the string's
// content into MOLD_BUF for any quoted form to be used by the caller.  It's
// overwritten in successive calls, and is only done for quoted forms (e.g.
// %"foo" will have data in MOLD_BUF but %foo will not.)
//
// !!! This is a somewhat weird separation of responsibilities, that seems to
// arise from a desire to make "Scan_XXX" functions independent of the
// "Locate_Token_May_Push_Mold" function.  But if the work of locating the
// value means you have to basically do what you'd do to read it into a REBVAL
// anyway, why split it?  This is especially true now that the variadic
// splicing pushes values directly from this routine.
//
// Error handling is limited for most types, as an additional phase is needed
// to load their data into a REBOL value.  Yet if a "cheap" error is
// incidentally found during this routine without extra cost to compute, it
// can fail here.
//
// Examples with ss's (B)egin (E)nd and return value:
//
//     foo: baz bar => TOKEN_SET
//     B   E
//
//     [quick brown fox] => TOKEN_BLOCK_BEGIN
//     B
//      E
//
//     "brown fox]" => TOKEN_WORD
//      B    E
//
//     $10AE.20 sent => fail()
//     B       E
//
//     {line1\nline2}  => TOKEN_STRING (content in MOLD_BUF)
//     B             E
//
//     \n{line2} => TOKEN_NEWLINE (newline is external)
//     BB
//       E
//
//     %"a ^"b^" c" d => TOKEN_FILE (content in MOLD_BUF)
//     B           E
//
//     %a-b.c d => TOKEN_FILE (content *not* in MOLD_BUF)
//     B     E
//
//     \0 => TOKEN_END
//     BB
//     EE
//
// Note: The reason that the code is able to use byte scanning over UTF-8
// encoded source is because all the characters that dictate the tokenization
// are currently in the ASCII range (< 128).
//
static void Locate_Token_May_Push_Mold(
    REB_MOLD *mo,
    SCAN_STATE *ss
) {
#if !defined(NDEBUG)
    TRASH_POINTER_IF_DEBUG(ss->end);
    ss->token = TOKEN_MAX; // trash token to help ensure it's recalculated
#endif

acquisition_loop:
    //
    // If a non-variadic scan of a UTF-8 string is being done, then ss->vaptr
    // will be NULL and ss->begin will be set to the data to scan.  A variadic
    // scan will start ss->begin at NULL also.
    //
    // Each time a string component being scanned gets exhausted, ss->begin
    // will be set to NULL and this loop is run to see if there's more input
    // to be processed.
    //
    while (ss->begin == NULL) {
        if (ss->vaptr == NULL) { // not a variadic va_list-based scan...
            ss->token = TOKEN_END; // ...so end of the utf-8 input was the end
            return;
        }

        const void *p = va_arg(*ss->vaptr, const void*);

        if (p == NULL) {
        #if defined(NDEBUG)
            ss->token = TOKEN_END;
            return;
        #else
            // NULL is just the integer 0, so it's bad to use to terminate C
            // va_lists, as integers may not be the same size as pointers and
            // it's easy to forget to cast.
            //
            panic ("NULL used to terminate va_list, use END instead");
        #endif
        }

        switch (Detect_Rebol_Pointer(p)) {
        case DETECTED_AS_END: {
            ss->token = TOKEN_END;
            return; }

        case DETECTED_AS_VALUE: {
            const REBVAL *splice = cast(const REBVAL*, p);
            DS_PUSH_TRASH;
            Move_Value(DS_TOP, splice);

            // !!! The needs of rebDo() are such that it wants to preserve
            // the non-user-visible EVAL_FLIP bit, which is usually not copied
            // by Move_Value.  This should not be allowed by rebBlock(),
            // however...so find a way to disable that in case someone puts
            // a rebEval() inside a rebBlock().
            //
            if (GET_VAL_FLAG(splice, VALUE_FLAG_EVAL_FLIP))
                SET_VAL_FLAG(DS_TOP, VALUE_FLAG_EVAL_FLIP);

            if (ss->newline_pending) {
                ss->newline_pending = FALSE;
                SET_VAL_FLAG(DS_TOP, VALUE_FLAG_LINE);
            }
            break; } // push values to emit stack until UTF-8 or END

        case DETECTED_AS_UTF8: {
            ss->begin = cast(const REBYTE*, p);

            // If we're using a va_list, we start the scan with no C string
            // pointer to serve as the beginning of line for an error message.
            // wing it by just setting the line pointer to whatever the start
            // of the first UTF-8 string fragment we see.
            //
            // !!! A more sophisticated debug mode might "reify" the va_list
            // as a BLOCK! before scanning, which might be able to give more
            // context for the error-causing input.
            //
            if (ss->line_head == NULL) {
                assert(ss->vaptr != NULL);
                assert(ss->start_line_head == NULL);
                ss->line_head = ss->start_line_head = ss->begin;
            }
            break; } // fallthrough to "ordinary" scanning

        default:
            panic ("Scanned pointer not END, REBVAL*, or valid UTF-8 string");
        }
    }

    REBCNT flags = Prescan_Token(ss); // sets ->begin, ->end

    const REBYTE *cp = ss->begin;

    switch (GET_LEX_CLASS(*cp)) {

    case LEX_CLASS_DELIMIT:
        switch (GET_LEX_VALUE(*cp)) {
        case LEX_DELIMIT_SPACE:
            panic ("Prescan_Token did not skip whitespace");

        case LEX_DELIMIT_SEMICOLON:     /* ; begin comment */
            while (NOT(ANY_CR_LF_END(*cp)))
                ++cp;
            if (*cp == '\0')
                --cp;             /* avoid passing EOF  */
            if (*cp == LF) goto line_feed;
            /* fall thru  */
        case LEX_DELIMIT_RETURN:
            if (cp[1] == LF)
                ++cp;
            /* fall thru */
        case LEX_DELIMIT_LINEFEED:
        line_feed:
            ss->line++;
            ss->end = cp + 1;
            ss->token = TOKEN_NEWLINE;
            return;


        // [BRACKETS]

        case LEX_DELIMIT_LEFT_BRACKET:
            ss->token = TOKEN_BLOCK_BEGIN;
            return;

        case LEX_DELIMIT_RIGHT_BRACKET:
            ss->token = TOKEN_BLOCK_END;
            return;

        // (PARENS)

        case LEX_DELIMIT_LEFT_PAREN:
            ss->token = TOKEN_GROUP_BEGIN;
            return;

        case LEX_DELIMIT_RIGHT_PAREN:
            ss->token = TOKEN_GROUP_END;
            return;


        // "QUOTES" and {BRACES}

        case LEX_DELIMIT_DOUBLE_QUOTE:
            cp = Scan_Quote_Push_Mold(mo, cp, ss);
            goto check_str;

        case LEX_DELIMIT_LEFT_BRACE:
            cp = Scan_Quote_Push_Mold(mo, cp, ss);
        check_str:
            if (cp) {
                ss->end = cp;
                ss->token = TOKEN_STRING;
                return;
            }
            // try to recover at next new line...
            cp = ss->begin + 1;
            while (NOT(ANY_CR_LF_END(*cp)))
                ++cp;
            ss->end = cp;
            ss->token = TOKEN_STRING;
            if (ss->begin[0] == '"')
                fail (Error_Missing(ss, '"'));
            if (ss->begin[0] == '{')
                fail (Error_Missing(ss, '}'));
            panic ("Invalid string start delimiter");

        case LEX_DELIMIT_RIGHT_BRACE:
            ss->token = TOKEN_STRING;
            fail (Error_Extra(ss, '}'));


        // /SLASH

        case LEX_DELIMIT_SLASH:
            while (*cp && *cp == '/')
                ++cp;
            if (
                IS_LEX_WORD_OR_NUMBER(*cp)
                || *cp == '+'
                || *cp == '-'
                || *cp == '.'
                || *cp == '|'
                || *cp == '_'
            ){
                // ///refine not allowed
                if (ss->begin + 1 != cp) {
                    ss->end = cp;
                    ss->token = TOKEN_REFINE;
                    fail (Error_Syntax(ss));
                }
                ss->begin = cp;
                TRASH_POINTER_IF_DEBUG(ss->end);
                flags = Prescan_Token(ss);
                ss->begin--;
                ss->token = TOKEN_REFINE;
                // Fast easy case:
                if (ONLY_LEX_FLAG(flags, LEX_SPECIAL_WORD))
                    return;
                goto scanword;
            }
            if (cp[0] == '<' || cp[0] == '>') {
                ss->end = cp + 1;
                ss->token = TOKEN_REFINE;
                fail (Error_Syntax(ss));
            }
            ss->end = cp;
            ss->token = TOKEN_WORD;
            return;

        case LEX_DELIMIT_END:
            //
            // We've reached the end of this string token's content.  By
            // putting a NULL in ss->begin, that will cue the acquisition loop
            // to check if there's a variadic pointer in effect to see if
            // there's more content yet to come.
            //
            ss->begin = NULL;
            TRASH_POINTER_IF_DEBUG(ss->end);
            goto acquisition_loop;

        case LEX_DELIMIT_UTF8_ERROR:
            ss->token = TOKEN_WORD;
            fail (Error_Syntax(ss));

        default:
            panic ("Invalid LEX_DELIMIT class");
        }

    case LEX_CLASS_SPECIAL:
        if (HAS_LEX_FLAG(flags, LEX_SPECIAL_AT) && *cp != '<') {
            ss->token = TOKEN_EMAIL;
            return;
        }
    next_ls:
        switch (GET_LEX_VALUE(*cp)) {

        case LEX_SPECIAL_AT:
            ss->token = TOKEN_EMAIL;
            fail (Error_Syntax(ss));

        case LEX_SPECIAL_PERCENT:       /* %filename */
            cp = ss->end;
            if (*cp == '"') {
                cp = Scan_Quote_Push_Mold(mo, cp, ss);
                ss->token = TOKEN_FILE;
                if (cp == NULL)
                    fail (Error_Syntax(ss));
                ss->end = cp;
                ss->token = TOKEN_FILE;
                return;
            }
            while (*cp == '/') {        /* deal with path delimiter */
                cp++;
                while (IS_LEX_NOT_DELIMIT(*cp))
                    ++cp;
            }
            ss->end = cp;
            ss->token = TOKEN_FILE;
            return;

        case LEX_SPECIAL_COLON:         /* :word :12 (time) */
            if (IS_LEX_NUMBER(cp[1])) {
                ss->token = TOKEN_TIME;
                return;
            }
            if (ONLY_LEX_FLAG(flags, LEX_SPECIAL_WORD)) {
                ss->token = TOKEN_GET;
                return; // common case
            }
            if (cp[1] == '\'') {
                ss->token = TOKEN_WORD;
                fail (Error_Syntax(ss));
            }
            // Various special cases of < << <> >> > >= <=
            if (cp[1] == '<' || cp[1] == '>') {
                cp++;
                if (cp[1] == '<' || cp[1] == '>' || cp[1] == '=')
                    ++cp;
                ss->token = TOKEN_GET;
                if (NOT(IS_LEX_DELIMIT(cp[1])))
                    fail (Error_Syntax(ss));
                ss->end = cp + 1;
                return;
            }
            ss->token = TOKEN_GET;
            ++cp; // skip ':'
            goto scanword;

        case LEX_SPECIAL_APOSTROPHE:
            if (IS_LEX_NUMBER(cp[1])) { // no '2nd
                ss->token = TOKEN_LIT;
                fail (Error_Syntax(ss));
            }
            if (cp[1] == ':') { // no ':X
                ss->token = TOKEN_LIT;
                fail (Error_Syntax(ss));
            }
            if (
                cp[1] == '|'
                && (IS_LEX_DELIMIT(cp[2]) || IS_LEX_ANY_SPACE(cp[2]))
            ){
                ss->token = TOKEN_LIT_BAR;
                return; // '| is a LIT-BAR!, '|foo is LIT-WORD!
            }
            if (ONLY_LEX_FLAG(flags, LEX_SPECIAL_WORD)) {
                ss->token = TOKEN_LIT;
                return; // common case
            }
            if (NOT(IS_LEX_WORD(cp[1]))) {
                // Various special cases of < << <> >> > >= <=
                if ((cp[1] == '-' || cp[1] == '+') && IS_LEX_NUMBER(cp[2])) {
                    ss->token = TOKEN_WORD;
                    fail (Error_Syntax(ss));
                }
                if (cp[1] == '<' || cp[1] == '>') {
                    cp++;
                    if (cp[1] == '<' || cp[1] == '>' || cp[1] == '=')
                        ++cp;
                    ss->token = TOKEN_LIT;
                    if (NOT(IS_LEX_DELIMIT(cp[1])))
                        fail (Error_Syntax(ss));
                    ss->end = cp + 1;
                    return;
                }
            }
            if (cp[1] == '\'') {
                ss->token = TOKEN_WORD;
                fail (Error_Syntax(ss));
            }
            ss->token = TOKEN_LIT;
            goto scanword;

        case LEX_SPECIAL_COMMA:         /* ,123 */
        case LEX_SPECIAL_PERIOD:        /* .123 .123.456.789 */
            SET_LEX_FLAG(flags, (GET_LEX_VALUE(*cp)));
            if (IS_LEX_NUMBER(cp[1]))
                goto num;
            ss->token = TOKEN_WORD;
            if (GET_LEX_VALUE(*cp) != LEX_SPECIAL_PERIOD)
                fail (Error_Syntax(ss));
            ss->token = TOKEN_WORD;
            goto scanword;

        case LEX_SPECIAL_GREATER:
            if (IS_LEX_DELIMIT(cp[1])) {
                ss->token = TOKEN_WORD;
                return;
            }
            if (cp[1] == '>') {
                ss->token = TOKEN_WORD;
                if (IS_LEX_DELIMIT(cp[2]))
                    return;
                fail (Error_Syntax(ss));
            }
            // falls through
        case LEX_SPECIAL_LESSER:
            if (IS_LEX_ANY_SPACE(cp[1]) || cp[1] == ']' || cp[1] == 0) {
                ss->token = TOKEN_WORD; // changed for </tag>
                return;
            }
            if (
                (cp[0] == '<' && cp[1] == '<') || cp[1] == '=' || cp[1] == '>'
            ){
                ss->token = TOKEN_WORD;
                if (IS_LEX_DELIMIT(cp[2]))
                    return;
                fail (Error_Syntax(ss));
            }
            if (
                cp[0] == '<' && (cp[1] == '-' || cp[1] == '|')
                && (IS_LEX_DELIMIT(cp[2]) || IS_LEX_ANY_SPACE(cp[2]))
            ){
                ss->token = TOKEN_WORD;
                return; // "<|" and "<-"
            }
            if (GET_LEX_VALUE(*cp) == LEX_SPECIAL_GREATER) {
                ss->token = TOKEN_WORD;
                fail (Error_Syntax(ss));
            }
            cp = Skip_Tag(cp);
            ss->token = TOKEN_TAG;
            if (cp == NULL)
                fail (Error_Syntax(ss));
            ss->end = cp;
            return;

        case LEX_SPECIAL_PLUS:          /* +123 +123.45 +$123 */
        case LEX_SPECIAL_MINUS:         /* -123 -123.45 -$123 */
            if (HAS_LEX_FLAG(flags, LEX_SPECIAL_AT)) {
                ss->token = TOKEN_EMAIL;
                return;
            }
            if (HAS_LEX_FLAG(flags, LEX_SPECIAL_DOLLAR)) {
                ss->token = TOKEN_MONEY;
                return;
            }
            if (HAS_LEX_FLAG(flags, LEX_SPECIAL_COLON)) {
                cp = Skip_To_Byte(cp, ss->end, ':');
                if (cp != NULL && (cp + 1) != ss->end) { // 12:34
                    ss->token = TOKEN_TIME;
                    return;
                }
                cp = ss->begin;
                if (cp[1] == ':') {     // +: -:
                    ss->token = TOKEN_WORD;
                    goto scanword;
                }
            }
            cp++;
            if (IS_LEX_NUMBER(*cp))
                goto num;
            if (IS_LEX_SPECIAL(*cp)) {
                if ((GET_LEX_VALUE(*cp)) >= LEX_SPECIAL_PERIOD)
                    goto next_ls;
                if (*cp == '+' || *cp == '-') {
                    ss->token = TOKEN_WORD;
                    goto scanword;
                }
                if (
                    *cp == '>'
                    && (IS_LEX_DELIMIT(cp[1]) || IS_LEX_ANY_SPACE(cp[1]))
                ) {
                    // Special exemption for ->
                    ss->token = TOKEN_WORD;
                    return;
                }
                ss->token = TOKEN_WORD;
                fail (Error_Syntax(ss));
            }
            ss->token = TOKEN_WORD;
            goto scanword;

        case LEX_SPECIAL_BAR:
            //
            // `|` standalone should become a BAR!, so if followed by a
            // delimiter or space.  However `|a|` and `a|b` are left as
            // legal words (at least for the time being).
            //
            if (IS_LEX_DELIMIT(cp[1]) || IS_LEX_ANY_SPACE(cp[1])) {
                ss->token = TOKEN_BAR;
                return;
            }
            if (
                cp[1] == '>'
                && (IS_LEX_DELIMIT(cp[2]) || IS_LEX_ANY_SPACE(cp[2]))
            ) {
                ss->token = TOKEN_WORD;
                return; // for `|>`
            }
            ss->token = TOKEN_WORD;
            goto scanword;

        case LEX_SPECIAL_BLANK:
            //
            // `_` standalone should become a BLANK!, so if followed by a
            // delimiter or space.  However `_a_` and `a_b` are left as
            // legal words (at least for the time being).
            //
            if (IS_LEX_DELIMIT(cp[1]) || IS_LEX_ANY_SPACE(cp[1])) {
                ss->token = TOKEN_BLANK;
                return;
            }
            ss->token = TOKEN_WORD;
            goto scanword;

        case LEX_SPECIAL_POUND:
        pound:
            cp++;
            if (*cp == '[') {
                ss->end = ++cp;
                ss->token = TOKEN_CONSTRUCT;
                return;
            }
            if (*cp == '"') { /* CHAR #"C" */
                REBUNI dummy;
                cp++;
                cp = Scan_UTF8_Char_Escapable(&dummy, cp);
                if (cp && *cp == '"') {
                    ss->end = cp + 1;
                    ss->token = TOKEN_CHAR;
                    return;
                }
                // try to recover at next new line...
                cp = ss->begin + 1;
                while (NOT(ANY_CR_LF_END(*cp)))
                    ++cp;
                ss->end = cp;
                ss->token = TOKEN_CHAR;
                fail (Error_Syntax(ss));
            }
            if (*cp == '{') { /* BINARY #{12343132023902902302938290382} */
                ss->end = ss->begin;  /* save start */
                ss->begin = cp;
                cp = Scan_Quote_Push_Mold(mo, cp, ss);
                ss->begin = ss->end;  /* restore start */
                if (cp) {
                    ss->end = cp;
                    ss->token = TOKEN_BINARY;
                    return;
                }
                // try to recover at next new line...
                cp = ss->begin + 1;
                while (NOT(ANY_CR_LF_END(*cp)))
                    ++cp;
                ss->end = cp;
                ss->token = TOKEN_BINARY;
                fail (Error_Syntax(ss));
            }
            if (cp - 1 == ss->begin) {
                ss->token = TOKEN_ISSUE;
                return;
            }

            ss->token = TOKEN_INTEGER;
            fail (Error_Syntax(ss));

        case LEX_SPECIAL_DOLLAR:
            if (HAS_LEX_FLAG(flags, LEX_SPECIAL_AT)) {
                ss->token = TOKEN_EMAIL;
                return;
            }
            ss->token = TOKEN_MONEY;
            return;

        default:
            ss->token = TOKEN_WORD;
            fail (Error_Syntax(ss));
        }

    case LEX_CLASS_WORD:
        ss->token = TOKEN_WORD;
        if (ONLY_LEX_FLAG(flags, LEX_SPECIAL_WORD))
            return;
        goto scanword;

    case LEX_CLASS_NUMBER:      /* order of tests is important */
    num:
        if (flags == 0) { // simple integer
            ss->token = TOKEN_INTEGER;
            return;
        }
        if (HAS_LEX_FLAG(flags, LEX_SPECIAL_AT)) {
            ss->token = TOKEN_EMAIL;
            return;
        }
        if (HAS_LEX_FLAG(flags, LEX_SPECIAL_POUND)) {
            if (cp == ss->begin) { // no +2 +16 +64 allowed
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
            ss->token = TOKEN_INTEGER;
            fail (Error_Syntax(ss));
        }
        if (HAS_LEX_FLAG(flags, LEX_SPECIAL_COLON)) { // 12:34
            ss->token = TOKEN_TIME;
            return;
        }
        if (HAS_LEX_FLAG(flags, LEX_SPECIAL_PERIOD)) {
            // 1.2 1.2.3 1,200.3 1.200,3 1.E-2
            if (Skip_To_Byte(cp, ss->end, 'x')) {
                ss->token = TOKEN_TIME;
                return;
            }
            cp = Skip_To_Byte(cp, ss->end, '.');
            // Note: no comma in bytes
            if (
                NOT(HAS_LEX_FLAG(flags, LEX_SPECIAL_COMMA))
                && Skip_To_Byte(cp + 1, ss->end, '.')
            ){
                ss->token = TOKEN_TUPLE;
                return;
            }
            ss->token = TOKEN_DECIMAL;
            return;
        }
        if (HAS_LEX_FLAG(flags, LEX_SPECIAL_COMMA)) {
            if (Skip_To_Byte(cp, ss->end, 'x')) {
                ss->token = TOKEN_PAIR;
                return;
            }
            ss->token = TOKEN_DECIMAL; // 1,23
            return;
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
            ){
                ss->token = TOKEN_INTEGER;
                fail (Error_Syntax(ss));
            }
            if (HAS_LEX_FLAG(flags, LEX_SPECIAL_PERIOD)) {
                ss->token = TOKEN_TUPLE;
                return;
            }
            ss->token = TOKEN_INTEGER;
            return;
        }
        /* Note: cannot detect dates of the form 1/2/1998 because they
        ** may appear within a path, where they are not actually dates!
        ** Special parsing is required at the next level up. */
        for (;cp != ss->end; cp++) {
            // what do we hit first? 1-AUG-97 or 123E-4
            if (*cp == '-') {
                ss->token = TOKEN_DATE;
                return; // 1-2-97 1-jan-97
            }
            if (*cp == 'x' || *cp == 'X') {
                ss->token = TOKEN_PAIR;
                return; // 320x200
            }
            if (*cp == 'E' || *cp == 'e') {
                if (Skip_To_Byte(cp, ss->end, 'x')) {
                    ss->token = TOKEN_PAIR;
                    return;
                }
                ss->token = TOKEN_DECIMAL; // 123E4
                return;
            }
            if (*cp == '%') {
                ss->token = TOKEN_PERCENT;
                return;
            }
        }
        ss->token = TOKEN_INTEGER;
        if (HAS_LEX_FLAG(flags, LEX_SPECIAL_APOSTROPHE)) // 1'200
            return;
        fail (Error_Syntax(ss));

    default:
        ; // put panic after switch, so no cases fall through
    }

    panic ("Invalid LEX class");

scanword:
#if !defined(NDEBUG)
    assert(ss->token != TOKEN_MAX);
#endif

    if (HAS_LEX_FLAG(flags, LEX_SPECIAL_COLON)) { // word:  url:words
        if (ss->token != TOKEN_WORD) {
            // only valid with WORD (not set or lit)
            return;
        }
        // This Skip_To_Byte always returns a pointer (always a ':')
        cp = Skip_To_Byte(cp, ss->end, ':');
        if (cp[1] != '/' && Lex_Map[cp[1]] < LEX_SPECIAL) {
            // a valid delimited word SET?
            if (
                HAS_LEX_FLAGS(
                    flags, ~LEX_FLAG(LEX_SPECIAL_COLON) & LEX_WORD_FLAGS
                )
            ){
                ss->token = TOKEN_WORD;
                fail (Error_Syntax(ss));
            }
            ss->token = TOKEN_SET;
            return;
        }
        cp = ss->end;   /* then, must be a URL */
        while (*cp == '/') {    /* deal with path delimiter */
            cp++;
            while (IS_LEX_NOT_DELIMIT(*cp) || *cp == '/')
                ++cp;
        }
        ss->end = cp;
        ss->token = TOKEN_URL;
        return;
    }
    if (HAS_LEX_FLAG(flags, LEX_SPECIAL_AT)) {
        ss->token = TOKEN_EMAIL;
        return;
    }
    if (HAS_LEX_FLAG(flags, LEX_SPECIAL_DOLLAR)) {
        ss->token = TOKEN_MONEY;
        return;
    }
    if (HAS_LEX_FLAGS(flags, LEX_WORD_FLAGS)) {
        // has chars not allowed in word (eg % \ )
        fail (Error_Syntax(ss));
    }
    if (HAS_LEX_FLAG(flags, LEX_SPECIAL_LESSER)) {
        // Allow word<tag> and word</tag> but not word< word<= word<> etc.
        cp = Skip_To_Byte(cp, ss->end, '<');
        if (
            cp[1] == '<' || cp[1] == '>' || cp[1] == '='
            || IS_LEX_SPACE(cp[1])
            || (cp[1] != '/' && IS_LEX_DELIMIT(cp[1]))
        ){
            fail (Error_Syntax(ss));
        }
        ss->end = cp;
    }
    else if (HAS_LEX_FLAG(flags, LEX_SPECIAL_GREATER))
        fail (Error_Syntax(ss));

    return;
}


//
//  Init_Va_Scan_State_Core: C
//
// Initialize a scanner state structure, using variadic C arguments.
//
void Init_Va_Scan_State_Core(
    SCAN_STATE *ss,
    REBSTR *file,
    REBUPT line,
    const REBYTE *opt_begin, // preload the scanner outside the va_list
    va_list *vaptr
){
    ss->vaptr = vaptr;

    ss->begin = opt_begin; // if NULL Locate_Token does first fetch from vaptr
    TRASH_POINTER_IF_DEBUG(ss->end);

    // !!! Splicing REBVALs into a scan as it goes creates complexities for
    // error messages based on line numbers.  Fortunately the splice of a
    // REBVAL* itself shouldn't cause a fail()-class error if there's no
    // data corruption, so it should be able to pick up *a* line head before
    // any errors occur...it just might not give the whole picture when used
    // to offer an error message of what's happening with the spliced values.
    //
    ss->start_line_head = ss->line_head = NULL;

    ss->start_line = ss->line = line;
    ss->file = file;

    ss->newline_pending = FALSE;

    ss->opts = 0;

#if !defined(NDEBUG)
    ss->token = TOKEN_MAX;
#endif
}


//
//  Init_Scan_State: C
//
// Initialize a scanner state structure.  Set the standard
// scan pointers and the limit pointer.
//
static void Init_Scan_State(
    SCAN_STATE *ss,
    REBSTR *file,
    REBUPT line,
    const REBYTE *utf8,
    REBCNT limit
){
    // The limit feature was not actually supported...just check to make sure
    // it's NUL terminated.
    //
    assert(utf8[limit] == '\0');
    UNUSED(limit);

    ss->vaptr = NULL; // signal Locate_Token to not use vaptr
    ss->begin = utf8;
    TRASH_POINTER_IF_DEBUG(ss->end);

    ss->start_line_head = ss->line_head = utf8;

    ss->start_line = ss->line = line;

    ss->newline_pending = FALSE;

    ss->file = file;
    ss->opts = 0;

#if !defined(NDEBUG)
    ss->token = TOKEN_MAX;
#endif
}


//
//  Scan_Head: C
//
// Search text for a REBOL header.  It is distinguished as
// the word REBOL followed by a '[' (they can be separated
// only by lines and comments).  There can be nothing on the
// line before the header.  Also, if a '[' preceedes the
// header, then note its position (for embedded code).
// The ss begin pointer is updated to point to the header block.
// Keep track of line-count.
//
// Returns:
//     0 if no header,
//     1 if header,
//    -1 if embedded header (inside []).
//
// The ss structure is updated to point to the
// beginning of the source text.
//
static REBINT Scan_Head(SCAN_STATE *ss)
{
    const REBYTE *rp = 0;   /* pts to the REBOL word */
    const REBYTE *bp = 0;   /* pts to optional [ just before REBOL */
    const REBYTE *cp = ss->begin;
    REBCNT count = ss->line;

    while (TRUE) {
        while (IS_LEX_SPACE(*cp)) cp++; /* skip white space */
        switch (*cp) {
        case '[':
            if (rp) {
                ss->begin = ++cp; //(bp ? bp : cp);
                ss->line = count;
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

    DEAD_END;
}


static REBARR *Scan_Full_Array(SCAN_STATE *ss, REBYTE mode_char);
static REBARR *Scan_Child_Array(SCAN_STATE *ss, REBYTE mode_char);

//
//  Scan_Array: C
//
// Scans an array of values, based on a mode_char.  This character can be
// '[', '(', or '/' to indicate the processing type.  Always returns array.
//
// If the source bytes are "1" then it will be the array [1]
// If the source bytes are "[1]" then it will be the array [[1]]
//
// Variations like GET-PATH!, SET-PATH! or LIT-PATH! are not discerned in
// the result here.  Instead, ordinary path scanning is done, followed by a
// transformation (e.g. if the first element was a GET-WORD!, change it to
// an ordinary WORD! and make it a GET-PATH!)  The caller does this.
//
REBARR *Scan_Array(
    SCAN_STATE *ss,
    REBYTE mode_char
){
    // The way that path scanning works is that after one item has been
    // scanned it is *retroactively* decided to begin picking up more items
    // in the path.  Hence, we take over one pushed item from the caller.
    //
    const REBDSP dsp_orig = (mode_char == '/') ? DSP - 1 : DSP;

    // just_once for load/next see Load_Script for more info.
    const REBOOL just_once = LOGICAL(ss->opts & SCAN_NEXT);

    struct Reb_State state;
    REBCTX *error;

    if (C_STACK_OVERFLOWING(&state))
        Trap_Stack_Overflow();

    if (ss->opts & SCAN_RELAX) {
        PUSH_TRAP(&error, &state);
        if (error != NULL) {
            ss->begin = ss->end; // skip malformed token

            DS_PUSH_TRASH;
            Init_Error(DS_TOP, error);

            goto array_done_relax;
        }
    }

    // We'd like to use DECLARE_MOLD here, but the macro has a problem with
    // goto crossing the initialization of `REB_MOLD *mo = &mold_struct`. :-(
    //
    REB_MOLD mo;
    CLEARS(&mo);

    if (just_once)
        ss->opts &= ~SCAN_NEXT; // no deeper

    while (
        Drop_Mold_If_Pushed(&mo),
        Locate_Token_May_Push_Mold(&mo, ss),
        (ss->token != TOKEN_END)
    ){
        assert(ss->begin != NULL && ss->end != NULL);
        assert(ss->begin < ss->end);

        const REBYTE *bp = ss->begin;
        const REBYTE *ep = ss->end;
        REBCNT len = cast(REBCNT, ep - bp);

        ss->begin = ss->end; // accept token

        // Process each lexical token appropriately:
        switch (ss->token) {

        case TOKEN_NEWLINE:
            ss->newline_pending = TRUE;
            ss->line_head = ep;
            continue;

        case TOKEN_BAR:
            DS_PUSH_TRASH;
            Init_Bar(DS_TOP);
            ++bp;
            break;

        case TOKEN_LIT_BAR:
            DS_PUSH_TRASH;
            Init_Lit_Bar(DS_TOP);
            ++bp;
            break;

        case TOKEN_BLANK:
            DS_PUSH_TRASH;
            Init_Blank(DS_TOP);
            ++bp;
            break;

        case TOKEN_LIT:
        case TOKEN_GET:
            if (ep[-1] == ':') {
                if (len == 1 || mode_char != '/')
                    fail (Error_Syntax(ss));
                --len;
                --ss->end;
            }
            bp++;
            // falls through
        case TOKEN_SET:
            len--;
            if (mode_char == '/' && ss->token == TOKEN_SET) {
                ss->token = TOKEN_WORD; // will be a PATH_SET
                ss->end--;  // put ':' back on end but not beginning
            }
            // falls through
        case TOKEN_WORD: {
            if (len == 0) {
                --bp;
                fail (Error_Syntax(ss));
            }

            REBSTR *spelling = Intern_UTF8_Managed(bp, len);
            DS_PUSH_TRASH;
            Init_Any_Word(
                DS_TOP, KIND_OF_WORD_FROM_TOKEN(ss->token), spelling
            );
            break; }

        case TOKEN_REFINE: {
            REBSTR *spelling = Intern_UTF8_Managed(bp + 1, len - 1);
            DS_PUSH_TRASH;
            Init_Refinement(DS_TOP, spelling);
            break; }

        case TOKEN_ISSUE:
            if (len == 1) {
                if (bp[1] == '(') {
                    ss->token = TOKEN_CONSTRUCT;
                    fail (Error_Syntax(ss));
                }
                DS_PUSH_TRASH;
                Init_Blank(DS_TOP);  // A single # means NONE
            }
            else {
                DS_PUSH_TRASH;
                if (ep != Scan_Issue(DS_TOP, bp + 1, len - 1))
                    fail (Error_Syntax(ss));
            }
            break;

        case TOKEN_BLOCK_BEGIN:
        case TOKEN_GROUP_BEGIN: {
            REBARR *array = Scan_Child_Array(
                ss, (ss->token == TOKEN_BLOCK_BEGIN) ? ']' : ')'
            );

            ep = ss->end;

            DS_PUSH_TRASH;
            Init_Any_Array(
                DS_TOP,
                (ss->token == TOKEN_BLOCK_BEGIN) ? REB_BLOCK : REB_GROUP,
                array
            );
            break; }

        case TOKEN_PATH:
            break;

        case TOKEN_BLOCK_END: {
            if (mode_char == ']')
                goto array_done;

            if (mode_char != 0) // expected a `)` or otherwise before the `]`
                fail (Error_Mismatch(ss, mode_char, ']'));

            // just a stray unexpected ']'
            //
            fail (Error_Extra(ss, ']')); }

        case TOKEN_GROUP_END: {
            if (mode_char == ')')
                goto array_done;

            if (mode_char != 0) // expected a ']' or otherwise before the ')'
                fail (Error_Mismatch(ss, mode_char, ')'));

            // just a stray unexpected ')'
            //
            fail (Error_Extra(ss, ')')); }

        case TOKEN_INTEGER:     // or start of DATE
            if (*ep != '/' || mode_char == '/') {
                DS_PUSH_TRASH;
                if (ep != Scan_Integer(DS_TOP, bp, len))
                    fail (Error_Syntax(ss));
            }
            else {              // A / and not in block
                ss->token = TOKEN_DATE;
                while (*ep == '/' || IS_LEX_NOT_DELIMIT(*ep))
                    ++ep;
                ss->begin = ep;
                len = cast(REBCNT, ep - bp);
                DS_PUSH_TRASH;
                if (ep != Scan_Date(DS_TOP, bp, len))
                    fail (Error_Syntax(ss));
            }
            break;

        case TOKEN_DECIMAL:
        case TOKEN_PERCENT:
            // Do not allow 1.2/abc:
            if (*ep == '/')
                fail (Error_Syntax(ss));

            DS_PUSH_TRASH;
            if (ep != Scan_Decimal(DS_TOP, bp, len, FALSE))
                fail (Error_Syntax(ss));

            if (bp[len - 1] == '%') {
                VAL_RESET_HEADER(DS_TOP, REB_PERCENT);
                VAL_DECIMAL(DS_TOP) /= 100.0;
            }
            break;

        case TOKEN_MONEY:
            // Do not allow $1/$2:
            if (*ep == '/') {
                ++ep;
                fail (Error_Syntax(ss));
            }

            DS_PUSH_TRASH;
            if (ep != Scan_Money(DS_TOP, bp, len))
                fail (Error_Syntax(ss));
            break;

        case TOKEN_TIME:
            if (bp[len-1] == ':' && mode_char == '/') { // could be path/10: set
                DS_PUSH_TRASH;
                if (ep - 1 != Scan_Integer(DS_TOP, bp, len - 1))
                    fail (Error_Syntax(ss));
                ss->end--;  // put ':' back on end but not beginning
                break;
            }
            DS_PUSH_TRASH;
            if (ep != Scan_Time(DS_TOP, bp, len))
                fail (Error_Syntax(ss));
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
                ss->begin = ep;  // End point extended to cover time
            }
            DS_PUSH_TRASH;
            if (ep != Scan_Date(DS_TOP, bp, len))
                fail (Error_Syntax(ss));
            break;

        case TOKEN_CHAR:
            DS_PUSH_TRASH;
            bp += 2; // skip #", and subtract 1 from ep for "
            if (ep - 1 != Scan_UTF8_Char_Escapable(&VAL_CHAR(DS_TOP), bp))
                fail (Error_Syntax(ss));
            VAL_RESET_HEADER(DS_TOP, REB_CHAR);
            break;

        case TOKEN_STRING: {
            // During scan above, string was stored in MOLD_BUF (UTF-8)
            //
            REBSER *s = Pop_Molded_String(&mo);
            DS_PUSH_TRASH;
            Init_String(DS_TOP, s);
            break; }

        case TOKEN_BINARY:
            DS_PUSH_TRASH;
            if (ep != Scan_Binary(DS_TOP, bp, len))
                fail (Error_Syntax(ss));
            break;

        case TOKEN_PAIR:
            DS_PUSH_TRASH;
            if (ep != Scan_Pair(DS_TOP, bp, len))
                fail (Error_Syntax(ss));
            break;

        case TOKEN_TUPLE:
            DS_PUSH_TRASH;
            if (ep != Scan_Tuple(DS_TOP, bp, len))
                fail (Error_Syntax(ss));
            break;

        case TOKEN_FILE:
            DS_PUSH_TRASH;
            if (ep != Scan_File(DS_TOP, bp, len))
                fail (Error_Syntax(ss));
            break;

        case TOKEN_EMAIL:
            DS_PUSH_TRASH;
            if (ep != Scan_Email(DS_TOP, bp, len))
                fail (Error_Syntax(ss));
            break;

        case TOKEN_URL:
            DS_PUSH_TRASH;
            if (ep != Scan_URL(DS_TOP, bp, len))
                fail (Error_Syntax(ss));
            break;

        case TOKEN_TAG:
            DS_PUSH_TRASH;

            // The Scan_Any routine (only used here for tag) doesn't
            // know where the tag ends, so it scans the len.
            //
            if (ep - 1 != Scan_Any(DS_TOP, bp + 1, len - 2, REB_TAG))
                fail (Error_Syntax(ss));
            break;

        case TOKEN_CONSTRUCT: {
            REBARR *array = Scan_Full_Array(ss, ']');

            // !!! Should the scanner be doing binding at all, and if so why
            // just Lib_Context?  Not binding would break functions entirely,
            // but they can't round-trip anyway.  See #2262.
            //
            Bind_Values_All_Deep(ARR_HEAD(array), Lib_Context);

            if (ARR_LEN(array) == 0 || !IS_WORD(ARR_HEAD(array))) {
                DECLARE_LOCAL (temp);
                Init_Block(temp, array);
                fail (Error_Malconstruct_Raw(temp));
            }

            REBSYM sym = VAL_WORD_SYM(ARR_HEAD(array));
            if (IS_KIND_SYM(sym)) {
                enum Reb_Kind kind = KIND_FROM_SYM(sym);

                MAKE_FUNC dispatcher = Make_Dispatch[kind];

                if (dispatcher == NULL || ARR_LEN(array) != 2) {
                    DECLARE_LOCAL (temp);
                    Init_Block(temp, array);
                    fail (Error_Malconstruct_Raw(temp));
                }

                // !!! As written today, MAKE may call into the evaluator, and
                // hence a GC may be triggered.  Performing evaluations during
                // the scanner is a questionable idea, but at the very least
                // `array` must be guarded, and a data stack cell can't be
                // used as the destination...because a raw pointer into the
                // data stack could go bad on any DS_PUSH or DS_DROP.
                //
                DECLARE_LOCAL (cell);
                PUSH_GUARD_ARRAY(array);
                Init_Unreadable_Blank(cell);
                PUSH_GUARD_VALUE(cell);

                dispatcher(cell, kind, KNOWN(ARR_AT(array, 1))); // may fail()

                DS_PUSH_TRASH;
                Move_Value(DS_TOP, cell);
                DROP_GUARD_VALUE(cell);
                DROP_GUARD_ARRAY(array);
            }
            else {
                if (ARR_LEN(array) != 1) {
                    DECLARE_LOCAL (temp);
                    Init_Block(temp, array);
                    fail (Error_Malconstruct_Raw(temp));
                }

                // !!! Construction syntax allows the "type" slot to be one of
                // the literals #[false], #[true]... along with legacy #[none]
                // while the legacy #[unset] is no longer possible (but
                // could load some kind of erroring function value)
                //
                switch (sym) {
            #if !defined(NDEBUG)
                case SYM_NONE:
                    // Should be under a LEGACY flag...
                    DS_PUSH_TRASH;
                    Init_Blank(DS_TOP);
                    break;
            #endif

                case SYM_FALSE:
                    DS_PUSH_TRASH;
                    Init_Logic(DS_TOP, FALSE);
                    break;

                case SYM_TRUE:
                    DS_PUSH_TRASH;
                    Init_Logic(DS_TOP, TRUE);
                    break;

                default: {
                    DECLARE_LOCAL (temp);
                    Init_Block(temp, array);
                    fail (Error_Malconstruct_Raw(temp)); }
                }
            }
            break; } // case TOKEN_CONSTRUCT

        case TOKEN_END:
            continue;

        default:
            panic ("Invalid TOKEN in Scanner.");
        }

        if (ss->newline_pending) {
            ss->newline_pending = FALSE;
            SET_VAL_FLAG(DS_TOP, VALUE_FLAG_LINE);
        }

        // Check for end of path:
        if (mode_char == '/') {
            if (*ep != '/')
                goto array_done;

            ep++;
            if (*ep != '(' && *ep != '[' && IS_LEX_DELIMIT(*ep)) {
                ss->token = TOKEN_PATH;
                fail (Error_Syntax(ss));
            }
            ss->begin = ep;  // skip next /
        }
        else if (*ep == '/') {
            //
            // We're noticing a path was actually starting with the token
            // that just got pushed, so it should be a part of that path.
            // So when `mode_char` is '/', it needs to steal this last one
            // pushed item from us...as it's the head of the path it couldn't
            // see coming in the future.

        #if !defined(NDEBUG)
            REBDSP dsp_check = DSP;
        #endif

            ++ss->begin;
            REBARR *array = Scan_Child_Array(ss, '/');

        #if !defined(NDEBUG)
            assert(DSP == dsp_check - 1); // should only take one!
        #endif

            if (ss->begin == NULL) {
                //
                // Something like trying to scan "*/", where there was no more
                // input to be had (begin is set to NULL, with the debug build
                // setting end to trash, to help catch this case)
                //
                ss->begin = bp;
                ss->end = ep + 1; // include the slash in error
                ss->token = TOKEN_PATH;
                fail (Error_Syntax(ss));
            }

            DS_PUSH_TRASH; // now push a path to take the stolen token's place

            if (ss->token == TOKEN_LIT) {
                VAL_RESET_HEADER(DS_TOP, REB_LIT_PATH);
                VAL_RESET_HEADER(ARR_HEAD(array), REB_WORD);
                assert(IS_WORD_UNBOUND(ARR_HEAD(array)));
            }
            else if (IS_GET_WORD(ARR_HEAD(array))) {
                if (*ss->end == ':')
                    fail (Error_Syntax(ss));
                VAL_RESET_HEADER(DS_TOP, REB_GET_PATH);
                VAL_RESET_HEADER(ARR_HEAD(array), REB_WORD);
                assert(IS_WORD_UNBOUND(ARR_HEAD(array)));
            }
            else {
                if (*ss->end == ':') {
                    VAL_RESET_HEADER(DS_TOP, REB_SET_PATH);
                    ss->begin = ++ss->end;
                }
                else
                    VAL_RESET_HEADER(DS_TOP, REB_PATH);
            }
            INIT_VAL_ARRAY(DS_TOP, array);
            VAL_INDEX(DS_TOP) = 0;
            ss->token = TOKEN_PATH;
        }

        // Added for load/next
        if (LOGICAL(ss->opts & SCAN_ONLY) || just_once)
            goto array_done;
    }

    // At some point, a token for an end of block or group needed to jump to
    // the array_done.  If it didn't, we never got a proper closing.
    //
    if (mode_char == ']' || mode_char == ')')
        fail (Error_Missing(ss, mode_char));

array_done:
    if (ss->opts & SCAN_RELAX)
        DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);

array_done_relax:
    Drop_Mold_If_Pushed(&mo);

    // The way that newline markers work is they sit on values inside the
    // array.  So if you wanted to preserve the newline on something like:
    //
    //     x: [
    //     ]
    //
    // That would require the ability to put a newline marker on END.  Rebol2
    // apparently let "END! values" carry the bit, so it would preserve the
    // above...but neither Red nor R3-Alpha preserve it and just get `[]`.
    // Preserving it in Ren-C would be hard due to Init_Endlike_Header().
    //
    // Since newline markers signify a newline *before* the element in
    // question, just discard it if there was one.
    //
    if (ss->newline_pending)
        ss->newline_pending = FALSE;

    // !!! Because a variadic rebDo() can have rebEval() entries, when it
    // delegates to the scanner that may mean it sees those entries.  This
    // should not be legal in constructors like rebBlock() since rebEval()
    // is not exposed there, so review how to prohibit it.  (See also
    // Pop_Stack_Values_Keep_Eval_Flip(), which we don't want to use here
    // since we're setting the file and line information from scan state.)
    //
    // All scanned code is expected to be managed by the GC (because walking
    // the tree after constructing it to add the "manage GC" bit would be
    // expensive, and we don't load source and free it manually anyway)
    //
    REBARR *result = Pop_Stack_Values_Core(
        dsp_orig,
        ARRAY_FLAG_VOIDS_LEGAL | NODE_FLAG_MANAGED
    );

    // Current thinking is that only arrays will preserve file and line info,
    // as the UTF-8 Everywhere change will likely need to use the ->misc and
    // ->link fields for caching purposes in strings.
    //
    MISC(result).line = ss->line;
    LINK(result).file = ss->file;
    SET_SER_FLAG(result, SERIES_FLAG_FILE_LINE);

    return result;
}


//
//  Scan_Child_Array: C
//
// This routine would create a new structure on the scanning stack.  Putting
// what would be local variables for each level into a structure helps with
// reflection, allowing for better introspection and error messages.  (This
// is similar to the benefits of Reb_Frame.)
//
static REBARR *Scan_Child_Array(SCAN_STATE *ss, REBYTE mode_char)
{
    SCAN_STATE child = *ss;

    // Capture current line and head of line into the starting points, because
    // some errors wish to report the start of the array's location.
    //
    child.start_line = ss->line;
    child.start_line_head = ss->line_head;
    child.newline_pending = FALSE;

    REBARR *result = Scan_Array(&child, mode_char);

    // The only variables that should actually be written back into the
    // parent ss are those reflecting an update in the "feed" of
    // data.  Here's a quick hack while the shape of that is discovered.

    REBCNT line_count = ss->line;
    const REBYTE *line_head = ss->line_head;
    enum Reb_Token token = ss->token;
    REBOOL newline_pending = ss->newline_pending;

    *ss = child;

    ss->line = line_count;
    ss->line_head = line_head;
    ss->token = token;
    ss->newline_pending = newline_pending;

    return result;
}


//
//  Scan_Full_Array: C
//
// Simple variation of scan_block to avoid problem with
// construct of aggregate values.
//
static REBARR *Scan_Full_Array(SCAN_STATE *ss, REBYTE mode_char)
{
    REBOOL saved_only = LOGICAL(ss->opts & SCAN_ONLY);
    ss->opts &= ~SCAN_ONLY;

    REBARR *array = Scan_Child_Array(ss, mode_char);

    if (saved_only)
        ss->opts |= SCAN_ONLY;
    return array;
}


//
//  Scan_Va_Managed: C
//
// Variadic form of source scanning.  Due to the nature of REBNOD (see
// %sys-node.h), it's possible to feed the scanner with a list of pointers
// that may be to UTF-8 strings or to Rebol values.  The behavior is to
// "splice" in the values at the point in the scan that they occur, e.g.
//
//     REBVAL *item1 = ...;
//     REBVAL *item2 = ...;
//     REBVAL *item3 = ...;
//     REBSTR *filename = ...; // where to say code came from
//
//     REBARR *result = Scan_Va_Managed(filename,
//         "if not", item1, "[\n",
//             item2, "| print {Close brace separate from content}\n",
//         "] else [\n",
//             item3, "| print {Close brace with content}]\n",
//         END
//     );
//
// While the approach is flexible, any token must appear fully inside its
// UTF-8 string component.  So you can't--for instance--divide a scan up like
// ("{abc", "def", "ghi}") and get the STRING! {abcdefghi}.  On that note,
// ("a", "/", "b") produces `a / b` and not the PATH! `a/b`.
//
REBARR *Scan_Va_Managed(
    REBSTR *filename, // NOTE: va_start must get last parameter before ...
    ...
){
    SCAN_STATE ss;
    const REBUPT start_line = 1;

    va_list va;
    va_start(va, filename);

    Init_Va_Scan_State_Core(&ss, filename, start_line, NULL, &va);

    REBARR *array = Scan_Array(&ss, 0);

    // !!! While in practice every system has va_end() as a no-op, it's not
    // necessarily true from a standards point of view:
    //
    // https://stackoverflow.com/q/32259543/
    //
    // It needs to be called before the longjmp in fail() crosses this stack
    // level.  That means either PUSH_UNHALTABLE_TRAP here, or coming up with
    // some more generic mechanism to register cleanup code that runs during
    // the fail().
    //
    va_end(va);

    return array;
}


//
//  Scan_UTF8_Managed: C
//
// Scan source code. Scan state initialized. No header required.
//
REBARR *Scan_UTF8_Managed(REBSTR *filename, const REBYTE *utf8, REBCNT len)
{
    SCAN_STATE ss;
    const REBUPT start_line = 1;
    Init_Scan_State(&ss, filename, start_line, utf8, len);
    return Scan_Array(&ss, 0);
}


//
//  Scan_Header: C
//
// Scan for header, return its offset if found or -1 if not.
//
REBINT Scan_Header(const REBYTE *utf8, REBCNT len)
{
    SCAN_STATE ss;
    REBSTR * const filename = Canon(SYM___ANONYMOUS__);
    const REBUPT start_line = 1;
    Init_Scan_State(&ss, filename, start_line, utf8, len);

    REBINT result = Scan_Head(&ss);
    if (result == 0)
        return -1;

    const REBYTE *cp = ss.begin - 2;

    // Backup to start of it:
    if (result > 0) { // normal header found
        while (cp != utf8 && *cp != 'r' && *cp != 'R')
            --cp;
    } else {
        while (cp != utf8 && *cp != '[')
            --cp;
    }
    return cast(REBINT, cp - utf8);
}


//
//  Startup_Scanner: C
//
void Startup_Scanner(void)
{
    REBCNT n = 0;
    while (Token_Names[n] != NULL)
        ++n;
    assert(cast(enum Reb_Token, n) == TOKEN_MAX);

    Init_String(TASK_BUF_UTF8, Make_Unicode(1020));
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
//      source [binary!]
//          "Must be Unicode UTF-8 encoded"
//      /next
//          {Translate next complete value (blocks as single value)}
//      /only
//          "Translate only a single value (blocks dissected)"
//      /relax
//          {Do not cause errors - return error object as value in place}
//      /file
//          file-name [file! url!]
//      /line
//          line-number [integer!]
//  ]
//
REBNATIVE(transcode)
{
    INCLUDE_PARAMS_OF_TRANSCODE;

    // !!! Should the base name and extension be stored, or whole path?
    //
    REBSTR *filename = REF(file)
        ? STR(ARG(file_name))
        : Canon(SYM___ANONYMOUS__);

    REBUPT start_line = 1;
    if (REF(line)) {
        start_line = VAL_INT32(ARG(line_number));
        if (start_line <= 0)
            fail (ARG(line_number));
    }
    else
        start_line = 1;

    SCAN_STATE ss;
    Init_Scan_State(
        &ss,
        filename,
        start_line,
        VAL_BIN_AT(ARG(source)),
        VAL_LEN_AT(ARG(source))
    );

    if (REF(next))
        ss.opts |= SCAN_NEXT;
    if (REF(only))
        ss.opts |= SCAN_ONLY;
    if (REF(relax))
        ss.opts |= SCAN_RELAX;

    // The scanner always returns an "array" series.  So set the result
    // to a BLOCK! of the results.
    //
    // If the source data bytes are "1" then it will be the block [1]
    // if the source data is "[1]" then it will be the block [[1]]

    Init_Block(D_OUT, Scan_Array(&ss, 0));

    // Add a value to the tail of the result, representing the input
    // with position advanced past the content consumed by the scan.
    // (Returning a length 2 block is how TRANSCODE does a "multiple
    // return value, but #1916 discusses a possible "revamp" of this.)

    VAL_INDEX(ARG(source)) = ss.end - VAL_BIN_HEAD(ARG(source));
    Append_Value(VAL_ARRAY(D_OUT), ARG(source));

    return R_OUT;
}


//
//  Scan_Any_Word: C
//
// Scan word chars and make word symbol for it.
// This method gets exactly the same results as scanner.
// Returns symbol number, or zero for errors.
//
const REBYTE *Scan_Any_Word(
    REBVAL *out,
    enum Reb_Kind kind,
    const REBYTE *utf8,
    REBCNT len
) {
    SCAN_STATE ss;
    REBSTR * const filename = Canon(SYM___ANONYMOUS__);
    const REBUPT start_line = 1;
    Init_Scan_State(&ss, filename, start_line, utf8, len);

    DECLARE_MOLD (mo);

    Locate_Token_May_Push_Mold(mo, &ss);
    if (ss.token != TOKEN_WORD)
        return NULL;

    Init_Any_Word(out, kind, Intern_UTF8_Managed(utf8, len));
    Drop_Mold_If_Pushed(mo);
    return ss.begin; // !!! is this right?
}


//
//  Scan_Issue: C
//
// Scan an issue word, allowing special characters.
//
const REBYTE *Scan_Issue(REBVAL *out, const REBYTE *cp, REBCNT len)
{
    if (len == 0) return NULL; // will trigger error

    while (IS_LEX_SPACE(*cp)) cp++; /* skip white space */

    const REBYTE *bp = cp;

    REBCNT l = len;
    while (l > 0) {
        switch (GET_LEX_CLASS(*bp)) {

        case LEX_CLASS_DELIMIT:
            return NULL; // will trigger error

        case LEX_CLASS_SPECIAL: { // Flag all but first special char
            REBCNT c = GET_LEX_VALUE(*bp);
            if (!(LEX_SPECIAL_APOSTROPHE == c
                || LEX_SPECIAL_COMMA == c
                || LEX_SPECIAL_PERIOD == c
                || LEX_SPECIAL_PLUS == c
                || LEX_SPECIAL_MINUS == c
                || LEX_SPECIAL_TILDE == c
                || LEX_SPECIAL_BAR == c
                || LEX_SPECIAL_BLANK == c
                || LEX_SPECIAL_COLON == c
            )) {
                return NULL; // will trigger error
            }}
            // fallthrough
        case LEX_CLASS_WORD:
        case LEX_CLASS_NUMBER:
            bp++;
            l--;
            break;
        }
    }

    REBSTR *str = Intern_UTF8_Managed(cp, len);
    Init_Issue(out, str);
    return bp;
}
