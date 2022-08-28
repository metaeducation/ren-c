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
// Copyright 2012-2019 Ren-C Open Source Contributors
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

inline static bool Is_Dot_Or_Slash(char c)
  { return c == '/' or c == '.'; }

inline static bool Interstitial_Match(char c, char mode) {
    assert(mode == '/' or mode == '.');
    return c == mode;
}

//
// Maps each character to its lexical attributes, using
// a frequency optimized encoding.
//
// UTF8: The values C0, C1, F5 to FF never appear.
//
const Byte Lex_Map[256] =
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
    /* 2C ,   */    LEX_DELIMIT|LEX_DELIMIT_COMMA,
    /* 2D -   */    LEX_SPECIAL|LEX_SPECIAL_MINUS,
    /* 2E .   */    LEX_DELIMIT|LEX_DELIMIT_PERIOD,
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
    /* 3B ;   */    LEX_SPECIAL|LEX_SPECIAL_SEMICOLON,
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
    /* 7E ~   */    LEX_SPECIAL|LEX_SPECIAL_TILDE,
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
// Maps each character to its upper case value.  Done this way for speed.
// Note the odd cases in last block.
//
const Byte Upper_Case[256] =
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


// Maps each character to its lower case value.  Done this way for speed.
// Note the odd cases in last block.
//
const Byte Lower_Case[256] =
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
// Returns the numeric value for char, or nullptr for errors.
// 0 is a legal codepoint value which may be returned.
//
// Advances the cp to just past the last position.
//
// test: to-integer load to-binary mold to-char 1234
//
static const Byte* Scan_UTF8_Char_Escapable(Codepoint *out, const Byte* bp)
{
    const Byte* cp;
    Byte lex;

    Byte c = *bp;
    if (c == '\0')
        return nullptr;  // signal error if end of string

    if (c >= 0x80) {  // multibyte sequence
        if (not (bp = Back_Scan_UTF8_Char(out, bp, nullptr)))
            return nullptr;
        return bp + 1;  // Back_Scan advances one less than the full encoding
    }

    bp++;

    if (c != '^') {
        *out = c;
        return bp;
    }

    c = *bp;  // Must be ^ escaped char
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
        *out = '\t';  // tab character
        break;

    case '!':
        *out = '\036';  // record separator
        break;

    case '(':  // ^(tab) ^(1234)
        cp = bp; // restart location
        *out = 0;

        // Check for hex integers ^(1234)
        while ((lex = Lex_Map[*cp]) > LEX_WORD) {
            c = lex & LEX_VALUE;
            if (c == 0 and lex < LEX_NUMBER)
                break;
            *out = (*out << 4) + c;
            cp++;
        }
        if (*cp == ')') {
            cp++;
            return cp;
        }

        // Check for identifiers
        for (c = 0; c < ESC_MAX; c++) {
            if ((cp = Try_Diff_Bytes_Uncased(bp, cb_cast(Esc_Names[c])))) {
                if (cp != nullptr and *cp == ')') {
                    bp = cp + 1;
                    *out = Esc_Codes[c];
                    return bp;
                }
            }
        }
        return nullptr;

    default:
        *out = c;

        c = UP_CASE(c);
        if (c >= '@' and c <= '_')
            *out = c - '@';
        else if (c == '~')
            *out = 0x7f; // special for DEL
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
static const Byte* Scan_Quote_Push_Mold(
    REB_MOLD *mo,
    const Byte* src,
    SCAN_STATE *ss
){
    Push_Mold(mo);

    Codepoint term; // pick termination
    if (*src == '{')
        term = '}';
    else {
        assert(*src == '"');
        term = '"';
    }
    ++src;

    REBINT nest = 0;
    REBLEN lines = 0;
    while (*src != term or nest > 0) {
        Codepoint c = *src;

        switch (c) {
          case '\0':
            // TEXT! literals can have embedded "NUL"s if escaped, but an
            // actual `\0` codepoint in the scanned text is not legal.
            //
            return nullptr;

          case '^':
            if ((src = Scan_UTF8_Char_Escapable(&c, src)) == NULL)
                return NULL;
            --src;  // unlike Back_Scan_XXX, no compensation for ++src later
            break;

          case '{':
            if (term != '"')
                ++nest;
            break;

          case '}':
            if (term != '"' and nest > 0)
                --nest;
            break;

          case CR: {
            //
            // !!! Historically CR LF was scanned as just an LF.  While a
            // tolerant mode of the scanner might be created someday, for
            // the moment we are being more prescriptive.
            //
            enum Reb_Strmode strmode = STRMODE_NO_CR;
            if (strmode == STRMODE_CRLF_TO_LF) {
                if (src[1] == LF) {
                    ++src;
                    c = LF;
                    goto linefeed;
                }
            }
            else
                assert(strmode == STRMODE_NO_CR);
            fail (Error_Illegal_Cr(src, ss->begin)); }

          case LF:
          linefeed:
            if (term == '"')
                return nullptr;
            ++lines;
            break;

          default:
            if (c >= 0x80) {
                if ((src = Back_Scan_UTF8_Char(&c, src, nullptr)) == nullptr)
                    return nullptr;
            }
        }

        ++src;

        if (c == '\0')  // e.g. ^(00) or ^@
            fail (Error_Illegal_Zero_Byte_Raw());  // legal CHAR!, not string

        Append_Codepoint(mo->series, c);
    }

    ss->line += lines;

    ++src; // Skip ending quote or brace.
    return src;
}


//
//  Scan_Item_Push_Mold: C
//
// Scan as UTF8 an item like a file.  Handles *some* forms of escaping, which
// may not be a great idea (see notes below on how URL! moved away from that)
//
// Returns continuation point or NULL for error.  Puts result into the
// temporary mold buffer as UTF-8.
//
const Byte* Scan_Item_Push_Mold(
    REB_MOLD *mo,
    const Byte* bp,
    const Byte* ep,
    Byte opt_term,  // '\0' if file like %foo - '"' if file like %"foo bar"
    const Byte* opt_invalids
){
    assert(opt_term < 128);  // method below doesn't search for high chars

    Push_Mold(mo);

    while (bp != ep and *bp != opt_term) {
        Codepoint c = *bp;

        if (c == '\0')
            break;  // End of stream

        if ((opt_term == '\0') and IS_WHITE(c))
            break;  // Unless terminator like '"' %"...", any whitespace ends

        if (c < ' ')
            return nullptr;  // Ctrl characters not valid in filenames, fail

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
            Byte decoded;
            bp = Scan_Hex2(&decoded, bp + 1);
            if (bp == nullptr)
                return nullptr;
            c = decoded;
            --bp;
        }
        else if (c == '^') {  // Accept ^X encoded char:
            if (bp + 1 == ep)
                return nullptr;  // error if nothing follows ^
            if (not (bp = Scan_UTF8_Char_Escapable(&c, bp)))
                return nullptr;
            if (opt_term == '\0' and IS_WHITE(c))
                break;
            --bp;
        }
        else if (c >= 0x80) { // Accept UTF8 encoded char:
            if (not (bp = Back_Scan_UTF8_Char(&c, bp, nullptr)))
                return nullptr;
        }
        else if (opt_invalids and strchr(cs_cast(opt_invalids), c)) {
            //
            // Is char as literal valid? (e.g. () [] etc.)
            // Only searches ASCII characters.
            //
            return nullptr;
        }

        ++bp;

        if (c == '\0')  // e.g. ^(00) or ^@
            fail (Error_Illegal_Zero_Byte_Raw());  // legal CHAR!, not string

        Append_Codepoint(mo->series, c);
    }

    if (*bp != '\0' and *bp == opt_term)
        ++bp;

    return bp;
}


//
//  Skip_Tag: C
//
// Skip the entire contents of a tag, including quoted strings and newlines.
// The argument points to the opening '<'.  nullptr is returned on errors.
//
static const Byte* Skip_Tag(const Byte* cp)
{
    assert(*cp == '<');
    ++cp;

    while (*cp != '\0' and *cp != '>') {
        if (*cp == '"') {
            cp++;
            while (*cp != '\0' and *cp != '"')
                ++cp;
            if (*cp == '\0')
                return nullptr;
        }
        cp++;
    }

    if (*cp != '\0')
        return cp + 1;

    return nullptr;
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
    Context(*) error,
    SCAN_STATE *ss,
    REBLEN line,
    const Byte* line_head
){
    // This sets the "where" for the error (e.g. the stack).  But then it
    // overrides the NEAR and FILE and LINE.
    //
    // !!! The error should actually report both the file and line that is
    // running as well as the file and line being scanned.  Review.
    //
    Set_Location_Of_Error(error, TOP_FRAME);

    // Skip indentation (don't include in the NEAR)
    //
    const Byte* cp = line_head;
    while (IS_LEX_SPACE(*cp))
        ++cp;

    // Find end of line to capture in error message
    //
    REBLEN len = 0;
    const Byte* bp = cp;
    while (not ANY_CR_LF_END(*cp)) {
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
    Append_Ascii(mo->series, "(line ");
    Append_Int(mo->series, line);
    Append_Ascii(mo->series, ") ");
    Append_Utf8(mo->series, cs_cast(bp), len);

    ERROR_VARS *vars = ERR_VARS(error);
    Init_Text(&vars->nearest, Pop_Molded_String(mo));

    if (ss->file)
        Init_File(&vars->file, ss->file);
    else
        Init_Nulled(VAL(&vars->file));

    Init_Integer(&vars->line, ss->line);
}


//
//  Error_Syntax: C
//
// Catch-all scanner error handler.  Reports the name of the token that gives
// the complaint, and gives the substring of the token's text.  Populates
// the NEAR field of the error with the "current" line number and line text,
// e.g. where the end point of the token is seen.
//
// !!! Note: the scanning process had a `goto scan_error` as a single point
// at the end of the routine, but that made it harder to break on where the
// actual error was occurring.  Goto may be more beneficial and cut down on
// code size in some way, but having lots of calls help esp. when scanning
// during boot and getting error line numbers printf'd in the Wasm build.
//
static Context(*) Error_Syntax(SCAN_STATE *ss, enum Reb_Token token) {
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
    assert(ss->begin and not IS_POINTER_TRASH_DEBUG(ss->begin));
    assert(ss->end and not IS_POINTER_TRASH_DEBUG(ss->end));
    assert(ss->end >= ss->begin);

    DECLARE_LOCAL (token_name);
    Init_Text(token_name, Make_String_UTF8(Token_Names[token]));

    DECLARE_LOCAL (token_text);
    Init_Text(
        token_text,
        Make_Sized_String_UTF8(
            cs_cast(ss->begin), cast(REBLEN, ss->end - ss->begin)
        )
    );

    Context(*) error = Error_Scan_Invalid_Raw(token_name, token_text);
    Update_Error_Near_For_Line(error, ss, ss->line, ss->line_head);
    return error;
}


//
//  Error_Missing: C
//
// Caused by code like: `load "( abc"`.
//
// Note: This error is useful for things like multi-line input, because it
// indicates a state which could be reconciled by adding more text.  A
// better form of this error would walk the scan state stack and be able to
// report all the unclosed terms.
//
static Context(*) Error_Missing(SCAN_LEVEL *level, char wanted) {
    DECLARE_LOCAL (expected);
    Init_Text(expected, Make_Codepoint_String(wanted));

    Context(*) error = Error_Scan_Missing_Raw(expected);

    // We have two options of where to implicate the error...either the start
    // of the thing being scanned, or where we are now (or, both).  But we
    // only have the start line information for GROUP! and BLOCK!...strings
    // don't cause recursions.  So using a start line on a string would point
    // at the block the string is in, which isn't as useful.
    //
    if (wanted == ')' or wanted == ']')
        Update_Error_Near_For_Line(
            error,
            level->ss,
            level->start_line,
            level->start_line_head
        );
    else
        Update_Error_Near_For_Line(
            error,
            level->ss,
            level->ss->line,
            level->ss->line_head
        );
    return error;
}


//
//  Error_Extra: C
//
// For instance, `load "abc ]"`
//
static Context(*) Error_Extra(SCAN_STATE *ss, char seen) {
    DECLARE_LOCAL (unexpected);
    Init_Text(unexpected, Make_Codepoint_String(seen));

    Context(*) error = Error_Scan_Extra_Raw(unexpected);
    Update_Error_Near_For_Line(error, ss, ss->line, ss->line_head);
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
static Context(*) Error_Mismatch(SCAN_LEVEL *level, char wanted, char seen) {
    Context(*) error = Error_Scan_Mismatch_Raw(rebChar(wanted), rebChar(seen));
    Update_Error_Near_For_Line(
        error,
        level->ss,
        level->start_line,
        level->start_line_head
    );
    return error;
}


//
//  Prescan_Token: C
//
// This function updates `ss->begin` to skip past leading whitespace.  If the
// first character it finds after that is a LEX_DELIMITER (`"`, `[`, `)`, `{`,
// etc. or a space/newline) then it will advance the end position to just past
// that one character.  For all other leading characters, it will advance the
// end pointer up to the first delimiter class byte (but not include it.)
//
// If the first character is not a delimiter, then this routine also gathers
// a quick "fingerprint" of the special characters that appeared after it, but
// before a delimiter was found.  This comes from unioning LEX_SPECIAL_XXX
// flags of the bytes that are seen (plus LEX_SPECIAL_WORD if any legal word
// bytes were found in that range.)
//
// For example, if the input were `$#foobar[@`
//
// - The flags LEX_SPECIAL_POUND and LEX_SPECIAL_WORD would be set.
// - $ wouldn't add LEX_SPECIAL_DOLLAR (it is the first character)
// - @ wouldn't add LEX_SPECIAL_AT (it's after the LEX_CLASS_DELIMITER '['
//
// Note: The reason the first character's lexical class is not considered is
// because it's important to know it *exactly*, so the caller will use
// GET_LEX_CLASS(ss->begin[0]).  Fingerprinting just helps accelerate further
// categorization.
//
static LEXFLAGS Prescan_Token(SCAN_STATE *ss)
{
    assert(IS_POINTER_TRASH_DEBUG(ss->end));  // prescan only uses ->begin

    const Byte* cp = ss->begin;
    LEXFLAGS flags = 0;  // flags for all LEX_SPECIALs seen after ss->begin[0]

    while (IS_LEX_SPACE(*cp))  // skip whitespace (if any)
        ++cp;
    ss->begin = cp;  // don't count leading whitespace as part of token

    while (true) {
        switch (GET_LEX_CLASS(*cp)) {
          case LEX_CLASS_DELIMIT:
            if (cp == ss->begin) {
                //
                // Include the delimiter if it is the only character we
                // are returning in the range (leave it out otherwise)
                //
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
            ++cp;
            break;

          case LEX_CLASS_WORD:
            //
            // If something is in LEX_CLASS_SPECIAL it gets set in the flags
            // that are returned.  But if any member of LEX_CLASS_WORD is
            // found, then a flag will be set indicating that also.
            //
            SET_LEX_FLAG(flags, LEX_SPECIAL_WORD);
            while (IS_LEX_WORD_OR_NUMBER(*cp))
                ++cp;
            break;

          case LEX_CLASS_NUMBER:
            while (IS_LEX_NUMBER(*cp))
                ++cp;
            break;
        }
    }

    DEAD_END;
}

// We'd like to test the fingerprint for lex flags that would be in an arrow
// but all 16 bits are used.  Here's a set of everything *but* =.  It might
// be that backslash for invalid word is wasted and could be retaken if it
// were checked for another way.
//
#define LEX_FLAGS_ARROW_EXCEPT_EQUAL \
    (LEX_FLAG(LEX_SPECIAL_GREATER) | LEX_FLAG(LEX_SPECIAL_LESSER) | \
    LEX_FLAG(LEX_SPECIAL_PLUS) | LEX_FLAG(LEX_SPECIAL_MINUS) | \
    LEX_FLAG(LEX_SPECIAL_BAR))


//
//  Maybe_Locate_Token_May_Push_Mold: C
//
// Find the beginning and end character pointers for the next token in the
// scanner state.  If the scanner is being fed variadically by a list of UTF-8
// strings and REBVAL pointers, then any Rebol values encountered will be
// spliced into the array being currently gathered by pushing them to the data
// stack (as tokens can only be located in UTF-8 strings encountered).
//
// The scan state will be updated so that `ss->begin` has been moved past any
// leading whitespace that was pending in the buffer.  `ss->end` will hold the
// conclusion at a delimiter.  The calculated token will be returned.
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
// Newlines that should be internal to a non-ANY-ARRAY! type are included in
// the scanned range between the `begin` and `end`.  But newlines that are
// found outside of a string are returned as TOKEN_NEWLINE.  (These are used
// to set the CELL_FLAG_NEWLINE_BEFORE bits on the next value.)
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
// "Maybe_Locate_Token_May_Push_Mold" function.  But if work on locating the
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
static enum Reb_Token Maybe_Locate_Token_May_Push_Mold(
    Context(*)* error,
    REB_MOLD *mo,
    Frame(*) f
){
    SCAN_LEVEL *level = &f->u.scan;
    SCAN_STATE *ss = level->ss;
    TRASH_POINTER_IF_DEBUG(ss->end);  // this routine should set ss->end

  acquisition_loop:
    //
    // If a non-variadic scan of a UTF-8 string is being done, then ss->vaptr
    // is nullptr and ss->begin will be set to the data to scan.  A variadic
    // scan will start ss->begin at nullptr also.
    //
    // Each time a string component being scanned gets exhausted, ss->begin
    // will be set to nullptr and this loop is run to see if there's more
    // input to be processed.
    //
    while (not ss->begin) {
        if (f->feed->p == nullptr) {  // API null, can't be in feed, use BLANK
            assert(IS_BLANK(FEED_NULL_SUBSTITUTE_CELL));
            Init_Blank(PUSH());
            if (Get_Executor_Flag(SCAN, f, NEWLINE_PENDING)) {
                Clear_Executor_Flag(SCAN, f, NEWLINE_PENDING);
                Set_Cell_Flag(TOP, NEWLINE_BEFORE);
            }
        }
        else switch (Detect_Rebol_Pointer(f->feed->p)) {
          case DETECTED_AS_END:
            f->feed->p = &PG_Feed_At_End;
            return TOKEN_END;

          case DETECTED_AS_CELL: {
            Copy_Reified_Variadic_Feed_Cell(PUSH(), f->feed);
            if (Get_Executor_Flag(SCAN, f, NEWLINE_PENDING)) {
                Clear_Executor_Flag(SCAN, f, NEWLINE_PENDING);
                Set_Cell_Flag(TOP, NEWLINE_BEFORE);
            }
            break; }

          case DETECTED_AS_SERIES: {  // e.g. rebQ, rebU, or a rebR() handle
            option(Value(const*)) v = Try_Reify_Variadic_Feed_Series(f->feed);
            if (not v)
                goto get_next_variadic_pointer;

            Copy_Cell(PUSH(), unwrap(v));
            if (Get_Executor_Flag(SCAN, f, NEWLINE_PENDING)) {
                Clear_Executor_Flag(SCAN, f, NEWLINE_PENDING);
                Set_Cell_Flag(TOP, NEWLINE_BEFORE);
            }
            break; }

          case DETECTED_AS_UTF8: {  // String segment, scan it ordinarily.
            ss->begin = cast(const Byte*, f->feed->p);  // breaks the loop...

            // If we're using a va_list, we start the scan with no C string
            // pointer to serve as the beginning of line for an error message.
            // wing it by just setting the line pointer to whatever the start
            // of the first UTF-8 string fragment we see.
            //
            // !!! A more sophisticated debug mode might "reify" the va_list
            // as a BLOCK! before scanning, which might be able to give more
            // context for the error-causing input.
            //
            if (not ss->line_head) {
                assert(FEED_VAPTR(f->feed) or FEED_PACKED(f->feed));
                assert(not level->start_line_head);
                level->start_line_head = ss->line_head = ss->begin;
            }
            break; }

          default:
            assert(false);
        }

      get_next_variadic_pointer:

        if (FEED_VAPTR(f->feed))
            f->feed->p = va_arg(*unwrap(FEED_VAPTR(f->feed)), const void*);
        else
            f->feed->p = *FEED_PACKED(f->feed)++;
    }

    LEXFLAGS flags = Prescan_Token(ss);  // sets ->begin, ->end

    const Byte* cp = ss->begin;

    if (*cp == ':') {
        ss->end = cp + 1;
        return TOKEN_COLON;
    }
    if (*cp == '^') {
        ss->end = cp + 1;
        return TOKEN_CARET;
    }
    if (*cp == '@') {
        ss->end = cp + 1;
        return TOKEN_AT;
    }

    // If we hit a vertical bar we know we are trying to scan a WORD! (which
    // may become transformed into a SET-WORD! or similar).  Typically this is
    // a WORD! which is being scanned as the content between two vertical bars.
    // But there are exceptions to this:
    //
    // * We want tokens comprised solely of vertical bars to be able to stand
    //   alone as WORD!s.  So |, ||, ||| etc. are their actual representation
    //   -unless- they are in PATH!s or TUPLE!s, where they must be escaped
    //   (e.g. as |\||)
    //
    // * Other symbols when standing alone like |> want to not require escapes,
    //   so it makes a difference if you see (|>) as |> vs. (|>|) as >.
    //
    if (*cp == '|') {
        Push_Mold(mo);  // buffer to write escaped form, e.g. \| => |
        bool all_arrow_chars = true;  // won't use buffer with standalone words

        while (true) {
            ++cp;

            if (*cp == '|') {
                if (not all_arrow_chars) {  // this is end of escaped
                    ss->end = cp + 1;
                    return TOKEN_ESCAPED_WORD;  // knows to look in mold buffer
                }
                if (mo->series != nullptr)
                    Drop_Mold(mo);  // unescaped | means it can't be escaped
                if (cp[1] != '<' && cp[1] != '>'
                    && cp[1] != '-' && cp[1] != '+'
                    && cp[1] != '=' && cp[1] != '|'
                ){
                    ss->end = cp + 1;
                    return TOKEN_WORD;
                }
                continue;
            }

            if (*cp == '\\') {
                if (mo->series == nullptr) {  // dropped mold due to plain |
                    *error = Error_User("Can't mix | and \\| in same token");
                    return TOKEN_0;
                }
                ++cp;
                if (*cp != '|') {
                    *error = Error_User("Backslashes only for escaping | ATM");
                    return TOKEN_0;
                }
                all_arrow_chars = false;  // now we're escaping
                Append_Codepoint(mo->series, '|');
                continue;
            }

            if (all_arrow_chars) {  // seeing non-| for first time
                if (
                    *cp == '<' || *cp == '>'
                    || *cp == '-' || *cp == '+'
                    || *cp == '='
                ){
                    if (mo->series != nullptr)
                        Append_Codepoint(mo->series, *cp);
                    continue;
                }

                if (
                    LEX_CLASS_DELIMIT == GET_LEX_CLASS(*cp)
                    and *cp != '.'  // we treat |.| as WORD! of period
                    and *cp != '/'  // we treat |/| as WORD! of slash
                ){
                    ss->end = cp;  // something like `|)` or `|||,`
                    if (mo->series != nullptr)
                        Drop_Mold(mo);  // does not use mold buffer
                    return TOKEN_WORD;
                }

                // here we could be at something like ||: which is not valid
                // (because we give priority to |:|)

                if (mo->series == nullptr) {  // saw more than one | before char
                   *error = Error_User(
                        "Must escape words starting with | if not standalone"
                    );
                   return TOKEN_0;
                }
            }

            if (*cp == '\n') {
                *error = Error_User("Newlines not permitted in WORD! atm");
                return TOKEN_0;
            }
            if (*cp == '\0') {
                *error = Error_User("End of string scanning escaped WORD!");
                return TOKEN_0;
            }

            all_arrow_chars = false;

            Codepoint c;
            cp = Back_Scan_UTF8_Char(&c, cp, nullptr);  // incremented in loop
            Append_Codepoint(mo->series, c);
        }
    }

    enum Reb_Token token;  // only set if falling through to `scan_word`

    // Up-front, do a check for "arrow words".  This test bails out if any
    // non-arrow word characters are seen.  Arrow WORD!s are contiguous
    // sequences of *only* "<", ">", "-", "=", "+", and "|".  This covers
    // things like `-->` and `<=`, but also applies to things that *look*
    // like they would be tags... like `<>` or `<+>`, which are WORD!s.
    //
    if (
        0 == (flags & ~(  // check flags for any obvious non-arrow characters
            LEX_FLAGS_ARROW_EXCEPT_EQUAL
            // don't count LEX_SPECIAL_AT; only valid at head, so not in flags
            | LEX_FLAG(LEX_SPECIAL_COLON)  // may be last char if SET-WORD!
            | LEX_FLAG(LEX_SPECIAL_WORD)  // `=` is WORD!-character, sets this
        ))
    ){
        bool seen_angles = false;

        const Byte* temp = cp;
        while (
            (*temp == '<' and (seen_angles = true))
            or (*temp == '>' and (seen_angles = true))
            or *temp == '+' or *temp == '-'
            or *temp == '=' or *temp == '|'
        ){
            ++temp;
            if (temp != ss->end)
                continue;

            // There has been a change from where things like `<.>` are no
            // longer a TUPLE! with < and > in it, to where it's a TAG!; this
            // philosophy limits WORD!s like << or >> from being put in
            // PATH!s and TUPLE!s:
            //
            // https://forum.rebol.info/t/1702
            //
            // The collateral damage is that things like `>/<` are illegal for
            // the sake of simplicity.  Such rules could be reviewed at a
            // later date.
            //
            // This code was modified to drop out of arrow-word scanning when
            // > or < were seen and a . or / happened.  Previously it had said:
            //
            // "The prescan for </foo> thinks that it might be a PATH! like
            // `</foo` so it stops at the slash.  To solve this, we only
            // support the `</foo>` and <foo />` cases of slashes in TAG!.
            // We know this is not the latter, because we did not hit a
            // space while we were processing.  For the former case, we
            // look to see if we get to a `>` before we hit a delimiter."
            //
            // I think prescan has to be adjusted, so this `seen_angles`
            // might become some kind of assert.
            //
            if (seen_angles and (*temp == '/' or *temp == '.'))
                break;

            return TOKEN_WORD;
        }
        if (*temp == ':' and temp + 1 == ss->end) {
            ss->end = temp;
            return TOKEN_WORD;
        }
    }

    switch (GET_LEX_CLASS(*cp)) {
      case LEX_CLASS_DELIMIT:
        switch (GET_LEX_VALUE(*cp)) {
          case LEX_DELIMIT_SPACE:
            panic ("Prescan_Token did not skip whitespace");

          case LEX_DELIMIT_RETURN:
          delimit_return: {
            //
            // !!! Ren-C is attempting to rationalize and standardize Rebol
            // on line feeds only.  If for some reason we wanted a tolerant
            // mode, that tolerance would go here.  Note that this code does
            // not cover the case of CR that are embedded inside multi-line
            // string literals.
            //
            enum Reb_Strmode strmode = STRMODE_NO_CR;  // ss->strmode ?
            if (strmode == STRMODE_CRLF_TO_LF) {
                if (cp[1] == LF) {
                    ++cp;
                    goto delimit_line_feed;
                }
            }
            else
                assert(strmode == STRMODE_NO_CR);

            *error = Error_Illegal_Cr(cp, ss->begin);
            Update_Error_Near_For_Line(*error, ss, ss->line, ss->line_head);
            return TOKEN_0; }

          case LEX_DELIMIT_LINEFEED:
          delimit_line_feed:
            ss->line++;
            ss->end = cp + 1;
            return TOKEN_NEWLINE;

          case LEX_DELIMIT_LEFT_BRACKET:  // [BLOCK] begin
            return TOKEN_BLOCK_BEGIN;

          case LEX_DELIMIT_RIGHT_BRACKET:  // [BLOCK] end
            return TOKEN_BLOCK_END;

          case LEX_DELIMIT_LEFT_PAREN:  // (GROUP) begin
            return TOKEN_GROUP_BEGIN;

          case LEX_DELIMIT_RIGHT_PAREN:  // (GROUP) end
            return TOKEN_GROUP_END;

          case LEX_DELIMIT_DOUBLE_QUOTE:  // "QUOTES"
            cp = Scan_Quote_Push_Mold(mo, cp, ss);
            goto check_str;

          case LEX_DELIMIT_LEFT_BRACE:  // {BRACES}
            cp = Scan_Quote_Push_Mold(mo, cp, ss);

          check_str:
            if (cp) {
                ss->end = cp;
                return TOKEN_STRING;
            }
            // try to recover at next new line...
            cp = ss->begin + 1;
            while (not ANY_CR_LF_END(*cp))
                ++cp;
            ss->end = cp;
            if (ss->begin[0] == '"') {
                *error = Error_Missing(level, '"');
                return TOKEN_0;
            }
            if (ss->begin[0] == '{') {
                *error = Error_Missing(level, '}');
                return TOKEN_0;
            }
            panic ("Invalid string start delimiter");

          case LEX_DELIMIT_RIGHT_BRACE:
            *error = Error_Extra(ss, '}');
            return TOKEN_0;

          case LEX_DELIMIT_SLASH:  // a /REFINEMENT-style PATH!
            assert(*cp == '/');
            assert(ss->begin == cp);
            ss->end = cp + 1;
            return TOKEN_PATH;

          case LEX_DELIMIT_PERIOD:  // a .PREDICATE-style TUPLE!
            assert(*cp == '.');
            assert(ss->begin == cp);
            ss->end = cp + 1;
            return TOKEN_TUPLE;

          case LEX_DELIMIT_END:
            //
            // We've reached the end of this string token's content.  By
            // putting nullptr in ss->begin, that cues the acquisition loop
            // to check if there's a variadic pointer in effect to see if
            // there's more content yet to come.
            //
            ss->begin = nullptr;
            TRASH_POINTER_IF_DEBUG(ss->end);
            goto acquisition_loop;

          case LEX_DELIMIT_COMMA:
            ++cp;
            ss->end = cp;
            if (*cp == ',' or not IS_LEX_DELIMIT(*cp)) {
                ++ss->end;  // don't allow `,,` or `a,b` etc.
                *error = Error_Syntax(ss, TOKEN_COMMA);
                return TOKEN_0;
            }
            return TOKEN_COMMA;

          case LEX_DELIMIT_UTF8_ERROR:
            *error = Error_Syntax(ss, TOKEN_WORD);
            return TOKEN_0;

          default:
            panic ("Invalid LEX_DELIMIT class");
        }

      case LEX_CLASS_SPECIAL:
        assert(GET_LEX_VALUE(*cp) != LEX_SPECIAL_BAR);  // weird word, handled

        if (GET_LEX_VALUE(*cp) == LEX_SPECIAL_SEMICOLON) {  // begin comment
            while (not ANY_CR_LF_END(*cp))
                ++cp;
            if (*cp == '\0')
                return TOKEN_END;  // `load ";"` is [] with no newline on tail
            if (*cp == LF)
                goto delimit_line_feed;
            assert(*cp == CR);
            goto delimit_return;
        }

        if (
            HAS_LEX_FLAG(flags, LEX_SPECIAL_AT)  // @ anywhere but at the head
            and *cp != '<'  // want <foo="@"> to be a TAG!, not an EMAIL!
            and *cp != '\''  // want '@foo to be a ... ?
            and *cp != '#'  // want #@ to be an ISSUE! (charlike)
        ){
            if (*cp == '@') {  // consider `@a@b`, `@@`, etc. ambiguous
                *error = Error_Syntax(ss, TOKEN_EMAIL);
                return TOKEN_0;
            }

            token = TOKEN_EMAIL;
            goto prescan_subsume_all_dots;
        }

      next_lex_special:

        switch (GET_LEX_VALUE(*cp)) {
          case LEX_SPECIAL_AT:  // the case where @ is actually at the head
            assert(false);  // already taken care of
            panic ("@ dead end");

          case LEX_SPECIAL_PERCENT:  // %filename
            if (cp[1] == '%') {  // %% is WORD! exception
                if (not IS_LEX_DELIMIT(cp[2]) and cp[2] != ':') {
                    ss->end = cp + 3;
                    *error = Error_Syntax(ss, TOKEN_FILE);
                    return TOKEN_0;
                }
                ss->end = cp + 2;
                return TOKEN_WORD;
            }

            token = TOKEN_FILE;

          issue_or_file_token:  // issue jumps here, should set `token`
            assert(token == TOKEN_FILE or token == TOKEN_ISSUE);

            cp = ss->end;
            if (*cp == ';') {
                //
                // !!! Help catch errors when writing `#;` which is an easy
                // mistake to make thinking it's like `#:` and a way to make
                // a single character.
                //
                ss->end = cp;
                *error = Error_Syntax(ss, token);
                return TOKEN_0;
            }
            if (*cp == '"') {
                cp = Scan_Quote_Push_Mold(mo, cp, ss);
                if (not cp) {
                    *error = Error_Syntax(ss, token);
                    return TOKEN_0;
                }
                ss->end = cp;
                return token;
            }
            while (*cp == '~' or *cp == '/' or *cp == '.') {  // "delimiters"
                cp++;

                while (IS_LEX_NOT_DELIMIT(*cp))
                    ++cp;
            }

            ss->end = cp;
            return token;

          case LEX_SPECIAL_COLON:  // :word :12 (time)
            assert(false);  // !!! Time form not supported ATM (use 0:12)

            if (IS_LEX_NUMBER(cp[1])) {
                token = TOKEN_TIME;
                goto prescan_subsume_up_to_one_dot;
            }

            panic (": dead end");

          case LEX_SPECIAL_APOSTROPHE:
            while (*cp == '\'')  // get sequential apostrophes as one token
                ++cp;
            ss->end = cp;
            return TOKEN_APOSTROPHE;

          case LEX_SPECIAL_GREATER:  // arrow words like `>` handled above
            *error = Error_Syntax(ss, TOKEN_TAG);
            return TOKEN_0;

          case LEX_SPECIAL_LESSER:
            cp = Skip_Tag(cp);
            if (
                not cp  // couldn't find ending `>`
                or not (
                    IS_LEX_DELIMIT(*cp)
                    or IS_LEX_ANY_SPACE(*cp)  // `<abc>def` not legal
                )
            ){
                *error = Error_Syntax(ss, TOKEN_TAG);
                return TOKEN_0;
            }
            ss->end = cp;
            return TOKEN_TAG;

          case LEX_SPECIAL_PLUS:  // +123 +123.45 +$123
          case LEX_SPECIAL_MINUS:  // -123 -123.45 -$123
            if (HAS_LEX_FLAG(flags, LEX_SPECIAL_AT)) {
                token = TOKEN_EMAIL;
                goto prescan_subsume_all_dots;
            }
            if (HAS_LEX_FLAG(flags, LEX_SPECIAL_DOLLAR)) {
                ++cp;
                token = TOKEN_MONEY;
                goto prescan_subsume_up_to_one_dot;
            }
            if (HAS_LEX_FLAG(flags, LEX_SPECIAL_COLON)) {
                cp = Skip_To_Byte(cp, ss->end, ':');
                if (cp and (cp + 1) != ss->end) {  // 12:34
                    token = TOKEN_TIME;
                    goto prescan_subsume_up_to_one_dot;  // -596523:14:07.9999
                }
                cp = ss->begin;
                if (cp[1] == ':') {  // +: -:
                    token = TOKEN_WORD;
                    goto prescan_word;
                }
            }
            cp++;
            if (IS_LEX_NUMBER(*cp))
                goto num;
            if (IS_LEX_SPECIAL(*cp)) {
                if ((GET_LEX_VALUE(*cp)) == LEX_SPECIAL_WORD)
                    goto next_lex_special;
                if (*cp == '+' or *cp == '-') {
                    token = TOKEN_WORD;
                    goto prescan_word;
                }
                *error = Error_Syntax(ss, TOKEN_WORD);
                return TOKEN_0;
            }
            token = TOKEN_WORD;
            goto prescan_word;

          case LEX_SPECIAL_BAR:
            assert(false);  // doesn't currently happen, scanned separately
            token = TOKEN_WORD;
            goto prescan_word;

          case LEX_SPECIAL_BLANK:
            //
            // `_` standalone should become a BLANK!, so if followed by a
            // delimiter or space.  However `_a_` and `a_b` are left as
            // legal words (at least for the time being).
            //
            if (IS_LEX_DELIMIT(cp[1]) or IS_LEX_ANY_SPACE(cp[1]))
                return TOKEN_BLANK;
            token = TOKEN_WORD;
            goto prescan_word;

          case LEX_SPECIAL_POUND:
          pound:
            cp++;
            if (*cp == '[') {
                ss->end = ++cp;
                return TOKEN_CONSTRUCT;
            }
            if (*cp == '"') {  // CHAR #"C"
                Codepoint dummy;
                cp++;
                cp = Scan_UTF8_Char_Escapable(&dummy, cp);
                if (cp and *cp == '"') {
                    ss->end = cp + 1;
                    return TOKEN_CHAR;
                }
                // try to recover at next new line...
                cp = ss->begin + 1;
                while (not ANY_CR_LF_END(*cp))
                    ++cp;
                ss->end = cp;
                *error = Error_Syntax(ss, TOKEN_CHAR);
                return TOKEN_0;
            }
            if (*cp == '{') {  // BINARY #{12343132023902902302938290382}
                ss->end = ss->begin;  // save start
                ss->begin = cp;
                cp = Scan_Quote_Push_Mold(mo, cp, ss);
                ss->begin = ss->end;  // restore start
                if (cp) {
                    ss->end = cp;
                    return TOKEN_BINARY;
                }
                // try to recover at next new line...
                cp = ss->begin + 1;
                while (not ANY_CR_LF_END(*cp))
                    ++cp;
                ss->end = cp;

                // !!! This was Error_Syntax(ss, TOKEN_BINARY), but if we use
                // the same error as for an unclosed string the console uses
                // that to realize the binary may be incomplete.  It may also
                // have bad characters in it, but that would be detected by
                // the caller, so we mention the missing `}` first.)
                //
                *error = Error_Missing(level, '}');
                return TOKEN_0;
            }
            if (cp - 1 == ss->begin) {
                --cp;
                token = TOKEN_ISSUE;
                goto issue_or_file_token;  // same policies on including `/`
            }

            *error = Error_Syntax(ss, TOKEN_INTEGER);
            return TOKEN_0;

          case LEX_SPECIAL_DOLLAR:
            if (
                cp[1] == '$' or cp[1] == ':' or IS_LEX_DELIMIT(cp[1])
            ){
                while (*cp == '$')
                    ++cp;
                ss->end = cp;
                return TOKEN_WORD;
            }
            if (HAS_LEX_FLAG(flags, LEX_SPECIAL_AT)) {
                token = TOKEN_EMAIL;
                goto prescan_subsume_all_dots;
            }
            token = TOKEN_MONEY;
            goto prescan_subsume_up_to_one_dot;

          case LEX_SPECIAL_TILDE: {
            ++cp;  // look at what comes after ~
            if (IS_LEX_DELIMIT(*cp)) {  // lone ~ is okay
                ss->end = cp;
                return TOKEN_BAD_WORD;
            }
            if (*cp == '~') {  // `~~` and `~~~a` etc are not legal
                while (not IS_LEX_DELIMIT(*cp))
                    ++cp;
                ss->end = cp;
                *error = Error_Syntax(ss, TOKEN_BAD_WORD);
                return TOKEN_0;
            }
            if (*cp == ':') {  // !!! Error here on `~:`, or would it anyway?
                ss->end = cp;
                *error = Error_Syntax(ss, TOKEN_BAD_WORD);
                return TOKEN_0;
            }
            for (; *cp != '~'; ++cp) {
                if (IS_LEX_DELIMIT(*cp)) {
                    ss->end = cp;
                    *error = Error_Syntax(ss, TOKEN_BAD_WORD);  // `[return ~a]`
                    return TOKEN_0;
                }
            }
            ss->end = cp + 1;
            return TOKEN_BAD_WORD; }

          default:
            *error = Error_Syntax(ss, TOKEN_WORD);
            return TOKEN_0;
        }

      case LEX_CLASS_WORD:
        if (ONLY_LEX_FLAG(flags, LEX_SPECIAL_WORD))
            return TOKEN_WORD;
        token = TOKEN_WORD;
        goto prescan_word;

      case LEX_CLASS_NUMBER:  // Note: "order of tests is important"
      num:;
        if (flags == 0)
            return TOKEN_INTEGER;  // simple integer e.g. `123`

        if (*(ss->end - 1) == ':') {  // terminal only valid if `a/1:`
            --ss->end;
            return TOKEN_INTEGER;
        }

        if (HAS_LEX_FLAG(flags, LEX_SPECIAL_AT)) {
            token = TOKEN_EMAIL;
            goto prescan_subsume_all_dots;  // `123@example.com`
        }

        if (HAS_LEX_FLAG(flags, LEX_SPECIAL_POUND)) {
            if (cp == ss->begin) {  // no +2 +16 +64 allowed
                if (
                    (
                        cp[0] == '6'
                        and cp[1] == '4'
                        and cp[2] == '#'
                        and cp[3] == '{'
                    ) or (
                        cp[0] == '1'
                        and cp[1] == '6'
                        and cp[2] == '#'
                        and cp[3] == '{'
                    ) // rare
                ) {
                    cp += 2;
                    goto pound;
                }
                if (cp[0] == '2' and cp[1] == '#' and cp[2] == '{') {
                    cp++;
                    goto pound;  // base-2 binary, "very rare"
                }
            }
            *error = Error_Syntax(ss, TOKEN_INTEGER);
            return TOKEN_0;
        }

        if (HAS_LEX_FLAG(flags, LEX_SPECIAL_COLON)) {
            token = TOKEN_TIME;  // `12:34`
            goto prescan_subsume_up_to_one_dot;
        }

        if (HAS_LEX_FLAG(flags, LEX_SPECIAL_POUND)) { // -#123 2#1010
            if (
                HAS_LEX_FLAGS(
                    flags,
                    ~(
                        LEX_FLAG(LEX_SPECIAL_POUND)
                        /* | LEX_FLAG(LEX_SPECIAL_PERIOD) */  // !!! What?
                        | LEX_FLAG(LEX_SPECIAL_APOSTROPHE)
                    )
                )
            ){
                *error = Error_Syntax(ss, TOKEN_INTEGER);
                return TOKEN_0;
            }
            return TOKEN_INTEGER;
        }

        // Note: R3-Alpha supported dates like `1/2/1998`, despite the main
        // date rendering showing as 2-Jan-1998.  This format was removed
        // because it is more useful to have `1/2` and other numeric-styled
        // PATH!s for use in dialecting.
        //
        for (; cp != ss->end; cp++) {
            // what do we hit first? 1-AUG-97 or 123E-4
            if (*cp == '-')
                return TOKEN_DATE;  // 1-2-97 1-jan-97
            if (*cp == 'x' or *cp == 'X')
                return TOKEN_PAIR;  // 320x200
            if (*cp == 'E' or *cp == 'e') {
                if (Skip_To_Byte(cp, ss->end, 'x'))
                    return TOKEN_PAIR;
                return TOKEN_DECIMAL;  // 123E4
            }
            if (*cp == '%')
                return TOKEN_PERCENT;

            if (Is_Dot_Or_Slash(*cp)) {  // will be part of a TUPLE! or PATH!
                ss->end = cp;
                return TOKEN_INTEGER;
            }
        }
        if (HAS_LEX_FLAG(flags, LEX_SPECIAL_APOSTROPHE))  // 1'200
            return TOKEN_INTEGER;
        *error = Error_Syntax(ss, TOKEN_INTEGER);
        return TOKEN_0;

      default:
        break;  // panic after switch, so no cases fall through accidentally
    }

    panic ("Invalid LEX class");

  prescan_word:  // `token` should be set, compiler warnings catch if not

    if (HAS_LEX_FLAG(flags, LEX_SPECIAL_COLON)) { // word:  url:words
        if (token != TOKEN_WORD)  // only valid with WORD (not set or lit)
            return token;
        cp = Skip_To_Byte(cp, ss->end, ':');
        assert(*cp == ':');
        if (not Is_Dot_Or_Slash(cp[1]) and Lex_Map[cp[1]] < LEX_SPECIAL) {
            // a valid delimited word SET?
            if (HAS_LEX_FLAGS(
                flags,
                ~LEX_FLAG(LEX_SPECIAL_COLON) & LEX_WORD_FLAGS
            )){
                *error = Error_Syntax(ss, TOKEN_WORD);
                return TOKEN_0;
            }
            --ss->end;  // don't actually include the colon
            return TOKEN_WORD;
        }
        cp = ss->end;  // then, must be a URL
        while (Is_Dot_Or_Slash(*cp)) {  // deal with path delimiter
            cp++;
            while (IS_LEX_NOT_DELIMIT(*cp) or not IS_LEX_DELIMIT_HARD(*cp))
                ++cp;
        }
        ss->end = cp;
        return TOKEN_URL;
    }
    if (HAS_LEX_FLAG(flags, LEX_SPECIAL_AT)) {
        token = TOKEN_EMAIL;
        goto prescan_subsume_all_dots;
    }
    if (HAS_LEX_FLAG(flags, LEX_SPECIAL_DOLLAR)) {  // !!! XYZ$10.20 ??
        token = TOKEN_MONEY;
        goto prescan_subsume_up_to_one_dot;
    }
    if (HAS_LEX_FLAGS(flags, LEX_WORD_FLAGS)) {
        *error = Error_Syntax(ss, TOKEN_WORD);  // has non-word chars (eg % \ )
        return TOKEN_0;
    }
    if (
        HAS_LEX_FLAG(flags, LEX_SPECIAL_LESSER)
        or HAS_LEX_FLAG(flags, LEX_SPECIAL_GREATER)
    ){
        *error = Error_Syntax(ss, token);  // arrow words handled at beginning
        return TOKEN_0;
    }

    return token;

  prescan_subsume_up_to_one_dot:
    assert(token == TOKEN_MONEY or token == TOKEN_TIME);

    // By default, `.` is a delimiter class which stops token scaning.  So if
    // scanning +$10.20 or -$10.20 or $3.04, there is common code to look
    // past the delimiter hit.  The same applies to times.  (DECIMAL! has
    // its own code)

    if (*ss->end != '.' and *ss->end != ',')
        return token;

    cp = ss->end + 1;
    while (not IS_LEX_DELIMIT(*cp) and not IS_LEX_ANY_SPACE(*cp))
        ++cp;
    ss->end = cp;

    return token;

  prescan_subsume_all_dots:
    assert(token == TOKEN_EMAIL);

    // Similar to the above, email scanning in R3-Alpha relied on the non
    // delimiter status of periods to incorporate them into the EMAIL!.
    // (Unlike FILE! or URL!, it did not already have code for incorporating
    // the otherwise-delimiting `/`)  It may be that since EMAIL! is not
    // legal in PATH! there's no real reason not to allow slashes in it, and
    // it could be based on the same code.
    //
    // (This is just good enough to lets the existing tests work on EMAIL!)

    if (*ss->end != '.')
        return token;

    cp = ss->end + 1;
    while (
        *cp == '.'
        or (not IS_LEX_DELIMIT(*cp) and not IS_LEX_ANY_SPACE(*cp))
    ){
        ++cp;
    }
    ss->end = cp;

    return token;
}


//
//  Init_Scan_Level: C
//
// Initialize a scanner state structure, using variadic C arguments.
//
void Init_Scan_Level(
    SCAN_LEVEL *level,
    SCAN_STATE *ss,
    String(const*) file,
    REBLIN line,
    option(const Byte*) bp
){
    level->ss = ss;

    ss->begin = try_unwrap(bp);  // Locate_Token's first fetch from vaptr
    TRASH_POINTER_IF_DEBUG(ss->end);

    ss->file = file;

    // !!! Splicing REBVALs into a scan as it goes creates complexities for
    // error messages based on line numbers.  Fortunately the splice of a
    // REBVAL* itself shouldn't cause a fail()-class error if there's no
    // data corruption, so it should be able to pick up *a* line head before
    // any errors occur...it just might not give the whole picture when used
    // to offer an error message of what's happening with the spliced values.
    //
    level->start_line_head = ss->line_head = ss->begin;
    level->start_line = ss->line = line;
    level->mode = '\0';
}


//
//  Scanner_Executor: C
//
// Scans values to the data stack, based on a mode.  This mode can be
// ']', ')', '/' or '.' to indicate the processing type...or '\0'.
//
// If the source bytes are "1" then it will push the INTEGER! 1
// If the source bytes are "[1]" then it will push the BLOCK! [1]
//
// BLOCK! and GROUP! use fairly ordinary recursions of this routine to make
// arrays.  PATH! scanning is a bit trickier...it starts after an element was
// scanned and is immediately followed by a `/`.  The stack pointer is marked
// to include that previous element, and a recursive call to Scan_To_Stack()
// collects elements so long as a `/` is seen between them.  When space is
// reached, the element that was seen prior to the `/` is integrated into a
// path to replace it in the scan of the array the path is in.  (e.g. if the
// prior element was a GET-WORD!, the scan becomes a GET-PATH!...if the final
// element is a SET-WORD!, the scan becomes a SET-PATH!)
//
Bounce Scanner_Executor(Frame(*) f) {
    Frame(*) frame_ = f;  // to use macros like OUT, SUBFRAME, etc.

    if (THROWING)
        return THROWN;  // no state to cleanup (just data stack, auto-cleaned)

    SCAN_LEVEL *level = &frame_->u.scan;
    SCAN_STATE *ss = level->ss;

    DECLARE_MOLD (mo);

    // "bp" and "ep" capture the beginning and end pointers of the token.
    // They may be manipulated during the scan process if desired.
    //
    const Byte* bp;
    const Byte* ep;
    REBLEN len;

    TRASH_POINTER_IF_DEBUG(bp);
    TRASH_POINTER_IF_DEBUG(ep);

    enum {
        ST_SCANNER_INITIAL_ENTRY = 0,
        ST_SCANNER_SCANNING_CHILD_ARRAY,
        ST_SCANNER_SCANNING_CONSTRUCT
    };

    switch (STATE) {
      case ST_SCANNER_INITIAL_ENTRY :
        goto initial_entry;

      case ST_SCANNER_SCANNING_CHILD_ARRAY :
        bp = ss->begin;
        ep = ss->end;
        len = cast(REBLEN, ep - bp);
        goto child_array_scanned;

      case ST_SCANNER_SCANNING_CONSTRUCT:
        bp = ss->begin;
        ep = ss->end;
        len = cast(REBLEN, ep - bp);
        goto construct_scan_to_stack_finished;

      default : assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    level->quotes_pending = 0;
    level->prefix_pending = TOKEN_0;

} loop: {  //////////////////////////////////////////////////////////////////

    Drop_Mold_If_Pushed(mo);
    Context(*) locate_error;
    level->token = Maybe_Locate_Token_May_Push_Mold(&locate_error, mo, f);

    if (level->token == TOKEN_0) {  // error signal
        assert(CTX_TYPE(locate_error) == REB_ERROR);
        return RAISE(locate_error);
    }

    if (level->token == TOKEN_END) {  // reached '\0'
        //
        // If we were scanning a BLOCK! or a GROUP!, then we should have hit
        // an ending `]` or `)` and jumped to `done`.  If an end token gets
        // hit first, there was never a proper closing.
        //
        if (level->mode == ']' or level->mode == ')')
            return RAISE(Error_Missing(level, level->mode));

        goto done;
      }

    assert(ss->begin and ss->end and ss->begin < ss->end);  // else good token

    bp = ss->begin;
    ep = ss->end;
    len = cast(REBLEN, ep - bp);

    ss->begin = ss->end;  // accept token

    switch (level->token) {
      case TOKEN_NEWLINE:
        Set_Executor_Flag(SCAN, f, NEWLINE_PENDING);
        ss->line_head = ep;
        goto loop;

      case TOKEN_BLANK:
        Init_Blank(PUSH());
        break;

      case TOKEN_BAD_WORD: {  // a non-isotope bad-word
        assert(*bp == '~');
        if (len == 1)
            Init_Meta_Of_Void(PUSH());
        else {
            assert(bp[len - 1] == '~');
            Symbol(const*) label = Intern_UTF8_Managed(bp + 1, len - 2);
            Init_Quasi_Word(PUSH(), label);
        }
        break; }

      case TOKEN_COMMA:
        if (level->mode == '/' or level->mode == '.') {
            //
            // We only see a comma during a PATH! or TUPLE! scan in cases where
            // a blank is needed.  So we'll get here with [/a/, xxx] but won't
            // get here with [/a, xxx]
            //
            Init_Blank(PUSH());
            ss->end = ss->begin = ep = bp;  // let parent see `,`
            goto done;
        }

        Init_Comma(PUSH());
        break;

      case TOKEN_CARET:
        assert(*bp == '^');
        if (IS_LEX_ANY_SPACE(*ep) or *ep == ']' or *ep == ')') {
            Init_Word(PUSH(), Canon(CARET_1));
            break;
        }
        goto token_prefixable_sigil;

      case TOKEN_AT:
        assert(*bp == '@');
        if (IS_LEX_ANY_SPACE(*ep) or *ep == ']' or *ep == ')') {
            Init_Word(PUSH(), Canon(AT_1));
            break;
        }
        goto token_prefixable_sigil;

      case TOKEN_COLON:
        assert(*bp == ':');

        // !!! If we are scanning a PATH! and see `:`, then classically that
        // could mean a GET-WORD! as they were allowed in paths.  Now the
        // only legal case of seeing a colon would be to end a PATH!, as
        // with `a/: 10`.  We temporarily discern the cases.
        //
        if (level->mode == '/' or level->mode == '.') {
            if (IS_LEX_ANY_SPACE(*ep) or *ep == ']' or *ep == ')') {
                Init_Blank(PUSH());  // `a.:` or `b/:` need a blank
                ss->end = ss->begin = ep = bp;  // let parent see `:`
                goto done;
            }

          #if !defined(NO_GET_WORDS_IN_PATHS)  // R3-Alpha compatibility hack
            //
            // !!! This is about the least invasive way to shove a GET-WORD!
            // into a PATH!, as trying to use ordinary token processing
            // only sets a pending get state which applies to the whole path,
            // not to individual tokens.
            //
            ++bp;
            ++ep;
            while (not (
                IS_LEX_ANY_SPACE(*ep)
                or IS_LEX_DELIMIT(*ep)
                or *ep == ':'  // The dreaded `foo/:x: 10` syntax
            )){
                ++ep;
            }
            Init_Get_Word(PUSH(), Intern_UTF8_Managed(bp, ep - bp));
            ss->begin = ss->end = ep;
            break;
          #else
            return RAISE(Error_Syntax(ss, token));
          #endif
        }

        goto token_prefixable_sigil;

      token_prefixable_sigil:
        if (level->prefix_pending != TOKEN_0)
            return RAISE(Error_Syntax(ss, level->token));  // no "GET-GET-WORD!"

        // !!! This is a hack to support plain colon.  It should support more
        // than one colon, but this gets a little further for now.  :-/
        //
        if (IS_LEX_ANY_SPACE(bp[1])) {
            Init_Word(PUSH(), Canon(COLON_1));
            break;
        }

        level->prefix_pending = level->token;
        goto loop;

      case TOKEN_ESCAPED_WORD:
        assert(bp[0] == '|' and bp[len - 1] == '|');
        Init_Word(
            PUSH(),
            Intern_UTF8_Managed(
                BIN_AT(mo->series, mo->base.size),
                BIN_LEN(mo->series) - mo->base.size
            )
        );
        Drop_Mold(mo);
        break;

      case TOKEN_WORD:
        if (len == 0)
            return RAISE(Error_Syntax(ss, level->token));

        Init_Word(PUSH(), Intern_UTF8_Managed(bp, len));
        break;

      case TOKEN_ISSUE:
        if (ep != Scan_Issue(PUSH(), bp + 1, len - 1))
            return DROP(), RAISE(Error_Syntax(ss, level->token));
        break;

      case TOKEN_APOSTROPHE: {
        assert(*bp == '\'');  // should be `len` sequential apostrophes

        if (level->prefix_pending != TOKEN_0)  // can't do @'foo: or :'foo
            return RAISE(Error_Syntax(ss, level->token));

        if (IS_LEX_ANY_SPACE(*ep) or *ep == ']' or *ep == ')' or *ep == ';') {
            //
            // If we have something like ['''] there won't be another token
            // push coming along to apply the quotes to, so quote a null.
            // This also applies to comments.
            //
            assert(level->quotes_pending == 0);
            Quotify(Init_Nulled(PUSH()), len);
        }
        else
            level->quotes_pending = len;  // apply quoting to next token
        goto loop; }

      case TOKEN_GROUP_BEGIN:
      case TOKEN_BLOCK_BEGIN: {
        Frame(*) subframe = Make_Frame(
            f->feed,
            FRAME_FLAG_TRAMPOLINE_KEEPALIVE  // we want accrued stack
                | (f->flags.bits & SCAN_EXECUTOR_MASK_RECURSE)
                | FRAME_FLAG_FAILURE_RESULT_OK
        );
        subframe->executor = &Scanner_Executor;

        subframe->u.scan.ss = ss;

        // Capture current line and head of line into the starting points.
        // (Some errors wish to report the start of the array's location.)
        //
        subframe->u.scan.start_line = ss->line;
        subframe->u.scan.start_line_head = ss->line_head;

        subframe->u.scan.mode = (level->token == TOKEN_BLOCK_BEGIN ? ']' : ')');
        STATE = ST_SCANNER_SCANNING_CHILD_ARRAY;
        Push_Frame(OUT, subframe);
        return CATCH_CONTINUE_SUBFRAME(subframe); }

 child_array_scanned: {  /////////////////////////////////////////////////////

        if (Is_Raised(OUT))
            goto handle_failure;

        Flags flags = NODE_FLAG_MANAGED;
        if (Get_Executor_Flag(SCAN, SUBFRAME, NEWLINE_PENDING))
            flags |= ARRAY_FLAG_NEWLINE_AT_TAIL;

        Array(*) a = Pop_Stack_Values_Core(
            SUBFRAME->baseline.stack_base,
            flags
        );
        Drop_Frame(SUBFRAME);

        // Tag array with line where the beginning bracket/group/etc. was found
        //
        a->misc.line = ss->line;
        mutable_LINK(Filename, a) = ss->file;
        Set_Subclass_Flag(ARRAY, a, HAS_FILE_LINE_UNMASKED);
        SET_SERIES_FLAG(a, LINK_NODE_NEEDS_MARK);

        enum Reb_Kind kind =
            (level->token == TOKEN_GROUP_BEGIN) ? REB_GROUP : REB_BLOCK;

        if (
            *ss->end == ':'  // `...(foo):` or `...[bar]:`
            and not Is_Dot_Or_Slash(level->mode)  // leave `:` for SET-PATH!
        ){
            Init_Array_Cell(PUSH(), SETIFY_ANY_PLAIN_KIND(kind), a);
            ++ss->begin;
            ++ss->end;
        }
        else
            Init_Array_Cell(PUSH(), kind, a);
        ep = ss->end;
        break; }

      case TOKEN_TUPLE:
        assert(*bp == '.');
        goto slash_or_dot_needs_blank_on_left;

      case TOKEN_PATH:
        assert(*bp == '/');
        goto slash_or_dot_needs_blank_on_left;

      slash_or_dot_needs_blank_on_left:
        assert(ep == bp + 1 and ss->begin == ep and ss->end == ep);

        // A "normal" path or tuple like `a/b/c` or `a.b.c` always has a token
        // on the left of the interstitial.  So the dot or slash gets picked
        // up by a lookahead step after this switch().
        //
        // This point is reached when a slash or dot gets seen "out-of-turn",
        // like `/a` or `a//b` or `a./b` etc
        //
        // Easiest thing to do here is to push a blank and then let whatever
        // processing would happen for a non-blank run (either start a new
        // path or tuple, or continuing one in progress).  So just do that
        // push and "unconsume" the token so the lookahead sees it.
        //
        Init_Blank(PUSH());
        ep = ss->begin = ss->end = bp;  // "unconsume" `.` or `/` token
        break;

      case TOKEN_BLOCK_END: {
        if (level->mode == ']')
            goto done;

        if (Is_Dot_Or_Slash(level->mode)) {  // implicit end, e.g. [just /]
            Init_Blank(PUSH());
            --ss->begin;
            --ss->end;
            goto done;
        }

        if (level->mode != '\0')  // expected e.g. `)` before the `]`
            return RAISE(Error_Mismatch(level, level->mode, ']'));

        // just a stray unexpected ']'
        //
        return RAISE(Error_Extra(ss, ']')); }

      case TOKEN_GROUP_END: {
        if (level->mode == ')')
            goto done;

        if (Is_Dot_Or_Slash(level->mode)) {  // implicit end e.g. (the /)
            Init_Blank(PUSH());
            --ss->begin;
            --ss->end;
            goto done;
        }

        if (level->mode != '\0')  // expected e.g. ']' before the ')'
            return RAISE(Error_Mismatch(level, level->mode, ')'));

        // just a stray unexpected ')'
        //
        return RAISE(Error_Extra(ss, ')')); }

      case TOKEN_INTEGER:
        //
        // We treat `10.20.30` as a TUPLE!, but `10.20` has a cultural lock on
        // being a DECIMAL! number.  Due to the overlap, Locate_Token() does
        // not have enough information in hand to discern TOKEN_DECIMAL; it
        // just returns TOKEN_INTEGER and the decision is made here.
        //
        // (Imagine we're in a tuple scan and INTEGER! 10 was pushed, and are
        // at "20.30" in the 10.20.30 case.  Locate_Token() would need access
        // to level->mode to know that the tuple scan was happening, else
        // it would have to conclude "20.30" was TOKEN_DECIMAL.  Deeper study
        // would be needed to know if giving Locate_Token() more information
        // is wise.  But that study would likely lead to the conclusion that
        // the whole R3-Alpha scanner concept needs a full rewrite!)
        //
        // Note: We can't merely start with assuming it's a TUPLE!, scan the
        // values, and then decide it's a DECIMAL! when the tuple is popped
        // if it's two INTEGER!.  Because the integer scanning will lose
        // leading digits on the second number (1.002 would become 1.2).
        //
        if (
            (*ep == '.' or *ep == ',')  // still allow `1,2` as `1.2` synonym
            and not Is_Dot_Or_Slash(level->mode)  // not in PATH!/TUPLE! (yet)
            and IS_LEX_NUMBER(ep[1])  // If # digit, we're seeing `###.#???`
        ){
            // If we will be scanning a TUPLE!, then we're at the head of it.
            // But it could also be a DECIMAL! if there aren't any more dots.
            //
            const Byte* temp = ep + 1;
            REBLEN temp_len = len + 1;
            for (; *temp != '.'; ++temp, ++temp_len) {
                if (IS_LEX_DELIMIT(*temp)) {
                    level->token = TOKEN_DECIMAL;
                    ss->begin = ss->end = ep = temp;
                    len = temp_len;
                    goto scan_decimal;
                }
            }
        }

        // Wasn't beginning of a DECIMAL!, so scan as a normal INTEGER!
        //
        if (ep != Scan_Integer(PUSH(), bp, len))
            return RAISE(Error_Syntax(ss, level->token));
        break;

      case TOKEN_DECIMAL:
      case TOKEN_PERCENT:
      scan_decimal:
        if (Is_Dot_Or_Slash(*ep))
            return RAISE(Error_Syntax(ss, level->token));  // No `1.2/abc`

        if (ep != Scan_Decimal(PUSH(), bp, len, false))
            return DROP(), RAISE(Error_Syntax(ss, level->token));

        if (bp[len - 1] == '%') {
            mutable_HEART_BYTE(TOP) = REB_PERCENT;

            // !!! DEBUG_EXTANT_STACK_POINTERS can't resolve if this is
            // a noquote(Cell(const*)) or REBVAL* overload with DEBUG_CHECK_CASTS.
            // Have to cast explicitly, use VAL()
            //
            VAL_DECIMAL(VAL(TOP)) /= 100.0;
        }
        break;

      case TOKEN_MONEY:
        if (Is_Dot_Or_Slash(*ep)) {  // Do not allow $1/$2
            ++ep;
            return RAISE(Error_Syntax(ss, level->token));
        }
        if (ep != Scan_Money(PUSH(), bp, len))
            return DROP(), RAISE(Error_Syntax(ss, level->token));
        break;

      case TOKEN_TIME:
        if (
            bp[len - 1] == ':'
            and Is_Dot_Or_Slash(level->mode)  // could be path/10: set
        ){
            if (ep - 1 != Scan_Integer(PUSH(), bp, len - 1))
                return DROP(), RAISE(Error_Syntax(ss, level->token));
            ss->end--;  // put ':' back on end but not beginning
            break;
        }
        if (ep != Scan_Time(PUSH(), bp, len))
            return DROP(), RAISE(Error_Syntax(ss, level->token));
        break;

      case TOKEN_DATE:
        while (*ep == '/' and level->mode != '/') {  // Is date/time?
            ep++;
            while (*ep == '.' or IS_LEX_NOT_DELIMIT(*ep))
                ++ep;
            len = cast(REBLEN, ep - bp);
            if (len > 50) {
                // prevent infinite loop, should never be longer than this
                break;
            }
            ss->begin = ep;  // End point extended to cover time
        }
        if (ep != Scan_Date(PUSH(), bp, len))
            return DROP(), RAISE(Error_Syntax(ss, level->token));
        break;

      case TOKEN_CHAR: {
        Codepoint uni;
        bp += 2;  // skip #", and subtract 1 from ep for "
        if (ep - 1 != Scan_UTF8_Char_Escapable(&uni, bp))
            return RAISE(Error_Syntax(ss, level->token));

        Context(*) error = Maybe_Init_Char(PUSH(), uni);
        if (error)
            return DROP(), RAISE(error);
        break; }

      case TOKEN_STRING:  // UTF-8 pre-scanned above, and put in MOLD_BUF
        Init_Text(PUSH(), Pop_Molded_String(mo));
        break;

      case TOKEN_BINARY:
        if (ep != Scan_Binary(PUSH(), bp, len))
            return DROP(), RAISE(Error_Syntax(ss, level->token));
        break;

      case TOKEN_PAIR:
        if (ep != Scan_Pair(PUSH(), bp, len))
            return DROP(), RAISE(Error_Syntax(ss, level->token));
        break;

      case TOKEN_FILE:
        if (ep != Scan_File(PUSH(), bp, len))
            return DROP(), RAISE(Error_Syntax(ss, level->token));
        break;

      case TOKEN_EMAIL:
        if (ep != Scan_Email(PUSH(), bp, len))
            return DROP(), RAISE(Error_Syntax(ss, level->token));
        break;

      case TOKEN_URL:
        if (ep != Scan_URL(PUSH(), bp, len))
            return DROP(), RAISE(Error_Syntax(ss, level->token));
        break;

      case TOKEN_TAG:
        //
        // The Scan_Any routine (only used here for tag) doesn't
        // know where the tag ends, so it scans the len.
        //
        if (ep - 1 != Scan_Any(
            PUSH(),
            bp + 1,
            len - 2,
            REB_TAG,
            STRMODE_NO_CR
        )){
            return DROP(), RAISE(Error_Syntax(ss, level->token));
        }
        break;

      case TOKEN_CONSTRUCT: {
        Frame(*) subframe = Make_Frame(
            f->feed,
            FRAME_FLAG_TRAMPOLINE_KEEPALIVE  // we want accrued stack
                | (f->flags.bits & SCAN_EXECUTOR_MASK_RECURSE)
                | FRAME_FLAG_FAILURE_RESULT_OK
        );
        subframe->executor = &Scanner_Executor;

        subframe->u.scan.ss = ss;

        // Capture current line and head of line into the starting points.
        // (Some errors wish to report the start of the array's location.)
        //
        subframe->u.scan.start_line = ss->line;
        subframe->u.scan.start_line_head = ss->line_head;

        subframe->u.scan.mode = ']';
        STATE = ST_SCANNER_SCANNING_CONSTRUCT;
        Push_Frame(OUT, subframe);
        return CATCH_CONTINUE_SUBFRAME(subframe); }

  construct_scan_to_stack_finished: {  ///////////////////////////////////////

        if (Is_Raised(OUT))
            goto handle_failure;

        Flags flags = NODE_FLAG_MANAGED;
        if (Get_Executor_Flag(SCAN, f, NEWLINE_PENDING))
            flags |= ARRAY_FLAG_NEWLINE_AT_TAIL;

        Array(*) array = Pop_Stack_Values_Core(
            SUBFRAME->baseline.stack_base,
            flags
        );

        Drop_Frame(SUBFRAME);

        // Tag array with line where the beginning bracket/group/etc. was found
        //
        array->misc.line = ss->line;
        mutable_LINK(Filename, array) = ss->file;
        Set_Subclass_Flag(ARRAY, array, HAS_FILE_LINE_UNMASKED);
        SET_SERIES_FLAG(array, LINK_NODE_NEEDS_MARK);

        // !!! Should the scanner be doing binding at all, and if so why
        // just Lib_Context?  Not binding would break functions entirely,
        // but they can't round-trip anyway.  See #2262.
        //
        Bind_Values_All_Deep(
            ARR_HEAD(array),
            ARR_TAIL(array),
            Lib_Context_Value
        );

        if (ARR_LEN(array) == 0 or not IS_WORD(ARR_HEAD(array))) {
            DECLARE_LOCAL (temp);
            Init_Block(temp, array);
            return RAISE(Error_Malconstruct_Raw(temp));
        }

        option(SymId) sym = VAL_WORD_ID(ARR_HEAD(array));
        if (
            IS_KIND_SYM(sym)
            or sym == SYM_IMAGE_X
        ){
            if (ARR_LEN(array) != 2) {
                DECLARE_LOCAL (temp);
                Init_Block(temp, array);
                return RAISE(Error_Malconstruct_Raw(temp));
            }

            // !!! Having an "extensible scanner" is something that has
            // not been designed.  So the syntax `#[image! [...]]` for
            // loading images doesn't have a strategy now that image is
            // not baked in.  It adds to the concerns the scanner already
            // has about evaluation, etc.  However, there are tests based
            // on this...so we keep them loading and working for now.

            // !!! As written today, MAKE may call into the evaluator, and
            // hence a GC may be triggered.  Performing evaluations during
            // the scanner is a questionable idea, but at the very least
            // `array` must be guarded, and a data stack cell can't be
            // used as the destination...because a raw pointer into the
            // data stack could go bad on any PUSH() or DROP().
            //
            PUSH_GC_GUARD(array);
            if (rebRunThrows(
                SPARE,  // can't write to movable stack location
                Canon(MAKE),  // will not work during boot!
                SPECIFIC(ARR_AT(array, 0)),
                rebQ(SPECIFIC(ARR_AT(array, 1)))  // e.g. ACTION! as WORD!
            )){
                CATCH_THROWN(OUT, frame_);
                DECLARE_LOCAL (temp);
                Init_Block(temp, array);
                return RAISE(Error_Malconstruct_Raw(temp));
            }
            DROP_GC_GUARD(array);

            Copy_Cell(PUSH(), SPARE);
        }
        else {
            if (ARR_LEN(array) != 1) {
                DECLARE_LOCAL (temp);
                Init_Block(temp, array);
                return RAISE(Error_Malconstruct_Raw(temp));
            }

            // !!! Construction syntax allows the "type" slot to be one of
            // the literals #[false], #[true]... along with legacy #[none]
            // while the legacy #[unset] is no longer possible (but
            // could load some kind of erroring function value)
            //
            switch (sym) {
              case SYM_NONE:  // !!! Should be under a LEGACY flag...
                Init_Blank(PUSH());
                break;

              case SYM_FALSE:
                Init_False(PUSH());
                break;

              case SYM_TRUE:
                Init_True(PUSH());
                break;

              case SYM_UNSET:  // !!! Should be under a LEGACY flag...
                Init_Quasi_Word(PUSH(), Canon(UNSET));
                break;

              default: {
                DECLARE_LOCAL (temp);
                Init_Block(temp, array);
                return RAISE(Error_Malconstruct_Raw(temp)); }
            }
        }
        break; }  // case TOKEN_CONSTRUCT

      case TOKEN_END:
        assert(false);  // handled way above, before the switch()

      default:
        panic ("Invalid TOKEN in Scanner.");
    }

    // We are able to bind code as we go into any "module", where the ambient
    // hashing is available.
    //
    // !!! While it wouldn't be impossible to do this with a binder for any
    // object, it would be more complex...only for efficiency, and nothing
    // like it existed before.
    //
    if (f->feed->context and ANY_WORD(TOP)) {
        INIT_VAL_WORD_BINDING(TOP, CTX_VARLIST(unwrap(f->feed->context)));
        INIT_VAL_WORD_INDEX(TOP, INDEX_ATTACHED);
    }

  lookahead:

    // At this point the item at TOP is the last token pushed.  It has
    // not had any `pending_prefix` or `pending_quotes` applied...so when
    // processing something like `:foo/bar` on the first step we'd only see
    // `foo` pushed.  This is the point where we look for the `/` or `.`
    // to either start or continue a tuple or path.

    if (Is_Dot_Or_Slash(level->mode)) {  // adding to existing path or tuple
        //
        // If we are scanning `a/b` and see `.c`, then we want the tuple
        // to stick to the `b`...which means using the `b` as the head
        // of a new child scan.
        //
        if (level->mode == '/' and *ep == '.') {
            level->token = TOKEN_TUPLE;
            ++ss->begin;
            goto scan_path_or_tuple_head_is_TOP;
        }

        // If we are scanning `a.b` and see `/c`, we want to defer to the
        // path scanning and consider the tuple finished.  This means we
        // want the level above to finish but then see the `/`.  Review.

        if (level->mode == '.' and *ep == '/') {
            level->token = TOKEN_PATH;  // ...?
            goto done;  // !!! need to return, but...?
        }

        if (not Interstitial_Match(*ep, level->mode))
            goto done;  // e.g. `a/b`, just finished scanning b

        ++ep;
        ss->begin = ep;

        if (
            *ep == '\0' or IS_LEX_SPACE(*ep) or ANY_CR_LF_END(*ep)
            or *ep == ')' or *ep == ']'
        ){
            goto done;
        }

        // Since we aren't "done" we are still in the sequence mode, which
        // means we don't want the "lookahead" to run and see any colons
        // that might be following, because it would apply them to the last
        // thing pushed...which is supposed to go internally to the sequence.
        //
        goto loop;
    }
    else if (Is_Dot_Or_Slash(*ep)) {  // starting a new path or tuple
        //
        // We're noticing a path was actually starting with the token
        // that just got pushed, so it should be a part of that path.

        ++ss->begin;

        if (*ep == '.')
            level->token = TOKEN_TUPLE;
        else
            level->token = TOKEN_PATH;

      scan_path_or_tuple_head_is_TOP: ;

        StackIndex stackindex_path_head = TOP_INDEX;

        if (
            *ss->begin == '\0'  // `foo/`
            or IS_LEX_ANY_SPACE(*ss->begin)  // `foo/ bar`
            or *ss->begin == ';'  // `foo/;bar`
        ){
            // Don't bother scanning recursively if we don't have to.
            // Note we still might come up empty (e.g. `foo/)`)
        }
        else {
            Frame(*) subframe = Make_Frame(
                f->feed,
                FRAME_FLAG_FAILURE_RESULT_OK
            );
            subframe->executor = &Scanner_Executor;

            SCAN_LEVEL *child = &subframe->u.scan;
            child->ss = ss;
            child->start_line = level->start_line;
            child->start_line_head = level->start_line_head;
            if (level->token == TOKEN_TUPLE)
                child->mode = '.';
            else
                child->mode = '/';

            Push_Frame(OUT, subframe);

            bool threw = Trampoline_With_Top_As_Root_Throws();

            Drop_Frame_Unbalanced(subframe);  // allow stack accrual

            if (threw)  // drop failing stack before throwing
                fail (Error_No_Catch_For_Throw(f));

            if (Is_Raised(OUT))
                return OUT;
        }

        // The scanning process for something like `.` or `a/` will not have
        // pushed anything to represent the last "blank".  Doing so would
        // require lookahead, which would overlap with this lookahead logic.
        // So notice if a trailing `.` or `/` requires pushing a blank.
        //
        if (ss->begin and (
            (level->token == TOKEN_TUPLE and *ss->end == '.')
            or (level->token == TOKEN_PATH and *ss->end == '/')
        )){
            Init_Blank(PUSH());
        }

        // R3-Alpha permitted GET-WORD! and other aberrations internally
        // to PATH!.  Ren-C does not, and it will optimize the immutable
        // GROUP! so that it lives in a cell (TBD).
        //
        // For interim compatibility, allow GET-WORD! at LOAD-time by
        // mutating it into a single element GROUP!.
        //
      blockscope {
        StackValue(*) head = Data_Stack_At(stackindex_path_head);
        StackValue(*) cleanup = head + 1;
        for (; cleanup <= TOP; ++cleanup) {
            if (IS_GET_WORD(cleanup)) {
                Array(*) a = Alloc_Singular(NODE_FLAG_MANAGED);
                mutable_HEART_BYTE(cleanup) = REB_GET_WORD;

                Move_Cell(ARR_SINGLE(a), cleanup);
                Init_Group(cleanup, a);
            }
        }
      }

        // Run through the generalized pop path code, which does any
        // applicable compression...and validates the array.
        //
        DECLARE_LOCAL (temp);

        // !!! The scanner needs an overhaul and rewrite to be less ad hoc.
        // Right now, dots act as delimiters for tuples which messes with
        // email addresses that contain dots.  It isn't obvious how to
        // patch support in for that, but we can notice when a tuple tries
        // to be made with an email address in it (which is not a legal
        // tuple) and mutate that into an email address.  Clearly this is
        // bad, but details of scanning isn't the focus at this time.
        //
        if (level->token == TOKEN_TUPLE) {
            bool any_email = false;
            StackIndex stackindex = TOP_INDEX;
            for (; stackindex != stackindex_path_head - 1; --stackindex) {
                if (IS_EMAIL(Data_Stack_At(stackindex))) {
                    if (any_email)
                        return RAISE(Error_Syntax(ss, level->token));
                    any_email = true;
                }
            }
            if (any_email) {
                //
                // There's one and only one email address.  Fuse the parts
                // together, inefficiently with usermode code.  (Recall that
                // this is an egregious hack in lieu of actually redesigning
                // the scanner, but still pretty cool we can do it this way.)
                //
                DECLARE_LOCAL (items);
                Init_Array_Cell(
                    items,
                    REB_THE_BLOCK,  // don't want to evaluate
                    Pop_Stack_Values(stackindex_path_head - 1)
                );
                PUSH_GC_GUARD(items);
                REBVAL *email = rebValue("as email! delimit {.}", items);
                DROP_GC_GUARD(items);
                Copy_Cell(temp, email);
                rebRelease(email);
                goto push_temp;
            }
        }

      blockscope {  // gotos would cross this initialization without
        REBVAL *check = Try_Pop_Sequence_Or_Element_Or_Nulled(
            temp,  // doesn't write directly to stack since popping stack
            level->token == TOKEN_TUPLE ? REB_TUPLE : REB_PATH,
            stackindex_path_head - 1
        );
        if (not check)
            return RAISE(Error_Syntax(ss, level->token));
      }

        assert(IS_WORD(temp) or ANY_SEQUENCE(temp));  // `/` and `...` decay

      push_temp:
        Copy_Cell(PUSH(), temp);

        // !!! Need to cover case where heart byte is a WORD!, at least when
        // it is something like `/` (refinements like /FOO should have been
        // bound when the words themselves were pushed).  This attachment may
        // be redundant in that case.  Review how this ties in with the
        // word attachment code above.
        //
        if (f->feed->context) {
            if (ANY_WORD(TOP)) {
                INIT_VAL_WORD_BINDING(TOP, CTX_VARLIST(unwrap(f->feed->context)));
                INIT_VAL_WORD_INDEX(TOP, INDEX_ATTACHED);
            }
        }

        // !!! Temporarily raise attention to usage like `.5` or `5.` to guide
        // people that these are contentious with tuples.  There is no way
        // to represent such tuples--while DECIMAL! has an alternative by
        // including the zero.  This doesn't put any decision in stone, but
        // reserves the right to make a decision at a later time.
        //
        if (IS_TUPLE(TOP) and VAL_SEQUENCE_LEN(TOP) == 2) {
            if (
                IS_INTEGER(VAL_SEQUENCE_AT(temp, TOP, 0))
                and IS_BLANK(VAL_SEQUENCE_AT(temp, TOP, 1))
            ){
                DROP();
                return RAISE("`5.` currently reserved, please use 5.0");
            }
            if (
                IS_BLANK(VAL_SEQUENCE_AT(temp, TOP, 0))
                and IS_INTEGER(VAL_SEQUENCE_AT(temp, TOP, 1))
            ){
                DROP();
                return RAISE("`.5` currently reserved, please use 0.5");
            }
        }

        // Can only store file and line information if it has an array
        //
        if (
            Get_Cell_Flag(TOP, FIRST_IS_NODE)
            and VAL_NODE1(TOP) != nullptr  // null legal in node slots ATM
            and IS_SER_ARRAY(SER(VAL_NODE1(TOP)))
        ){
            Array(*) a = ARR(VAL_NODE1(TOP));
            a->misc.line = ss->line;
            mutable_LINK(Filename, a) = ss->file;
            Set_Subclass_Flag(ARRAY, a, HAS_FILE_LINE_UNMASKED);
            SET_SERIES_FLAG(a, LINK_NODE_NEEDS_MARK);

            // !!! Does this mean anything for paths?  The initial code
            // had it, but it was exploratory and predates the ideas that
            // are currently being used to solidify paths.
            //
            if (Get_Executor_Flag(SCAN, f, NEWLINE_PENDING))
                Set_Subclass_Flag(ARRAY, a, NEWLINE_AT_TAIL);
        }

        if (level->token == TOKEN_TUPLE) {
            assert(level->mode != '.');  // shouldn't scan tuple-in-tuple!

            if (level->mode == '/') {
                //
                // If we were scanning a PATH! and interrupted it to scan
                // a tuple, then we did so at a moment that a `/` was
                // being tested for.  Now that we're resuming, we need
                // to pick that test back up and quit picking up tokens
                // if we don't see a `/` after that tuple we just scanned.
                //
                if (*ss->begin != '/')
                    goto done;

                ep = ss->end;
                goto lookahead;  // stay in path mode
            }
            else {
                // If we just finished a TUPLE! that was being scanned
                // all on its own (not as part of a path), then if a
                // slash follows, we want to process that like a PATH! on
                // the same level (otherwise we would start a new token,
                // and "a.b/c" would be `a.b /c`).
                //
                if (ss->begin != nullptr and *ss->begin == '/') {
                    ++ss->begin;
                    level->token = TOKEN_PATH;
                    goto scan_path_or_tuple_head_is_TOP;
                }
            }
        }
    }

    // If we get here without jumping somewhere else, we have pushed a
    // *complete* token (vs. just a component of a path).  While we know that
    // no whitespace has been consumed, this is a good time to tell that a
    // colon means "SET" and not "GET".  We also apply any pending prefix
    // or quote levels that were noticed at the beginning of a token scan,
    // but had to wait for the completed token to be used.

    if (ss->begin and *ss->begin == ':') {  // no whitespace, interpret as SET
        if (level->prefix_pending)
            return RAISE(Error_Syntax(ss, level->token));

        enum Reb_Kind kind = VAL_TYPE(TOP);
        if (not ANY_PLAIN_KIND(kind))
            return RAISE(Error_Syntax(ss, level->token));

        mutable_HEART_BYTE(TOP) = SETIFY_ANY_PLAIN_KIND(kind);

        ss->begin = ++ss->end;  // !!! ?
    }
    else if (level->prefix_pending != TOKEN_0) {
        enum Reb_Kind kind = VAL_TYPE(TOP);
        if (not ANY_PLAIN_KIND(kind))
            return DROP(), RAISE(Error_Syntax(ss, level->token));

        switch (level->prefix_pending) {
          case TOKEN_COLON:
            mutable_HEART_BYTE(TOP) = GETIFY_ANY_PLAIN_KIND(kind);
            break;

          case TOKEN_CARET:
            mutable_HEART_BYTE(TOP) = METAFY_ANY_PLAIN_KIND(kind);
            break;

          case TOKEN_AT:
            mutable_HEART_BYTE(TOP) = THEIFY_ANY_PLAIN_KIND(kind);
            break;

          default:
            level->token = level->prefix_pending;
            return DROP(), RAISE(Error_Syntax(ss, level->token));
        }
        level->prefix_pending = TOKEN_0;
    }

    if (level->quotes_pending != 0) {
        //
        // Transform the topmost value on the stack into a QUOTED!, to
        // account for the ''' that was preceding it.
        //
        Quotify(TOP, level->quotes_pending);
        level->quotes_pending = 0;
    }

    // Set the newline on the new value, indicating molding should put a
    // line break *before* this value (needs to be done after recursion to
    // process paths or other arrays...because the newline belongs on the
    // whole array...not the first element of it).
    //
    if (Get_Executor_Flag(SCAN, f, NEWLINE_PENDING)) {
        Clear_Executor_Flag(SCAN, f, NEWLINE_PENDING);
        Set_Cell_Flag(TOP, NEWLINE_BEFORE);
    }

    // Added for TRANSCODE/NEXT (LOAD/NEXT is deprecated, see #1703)
    //
    if (Get_Executor_Flag(SCAN, f, JUST_ONCE))
        goto done;

    goto loop;

} done: {  ///////////////////////////////////////////////////////////////////

    Drop_Mold_If_Pushed(mo);

    assert(level->quotes_pending == 0);
    assert(level->prefix_pending == TOKEN_0);

    // Note: ss->newline_pending may be true; used for ARRAY_NEWLINE_AT_TAIL

    return NONE;

} handle_failure: {  /////////////////////////////////////////////////////////

    assert(Is_Raised(OUT));

    Drop_Frame(SUBFRAME);  // could `return RAISE(VAL_CONTEXT(OUT))`
    return OUT;
}}


//
//  Scan_UTF8_Managed: C
//
// This is a "stackful" call that takes a buffer of UTF-8 and will try to
// scan it into an array, or raise an "abrupt" error (that won't be catchable
// by things like ATTEMPT or EXCEPT, only TRAP).
//
// 1. This routine doesn't offer parameterizatoin for variadic "splicing" of
//    already-loaded values mixed with the textual code as it's being
//    scanned.  (For that, see `rebTranscodeInto()`.)  But the underlying
//    scanner API requires a variadic feed to be provided...so we just pass
//    a simple 2-element feed in of [UTF-8 string, END]
//
// 2. This uses the "C++ form" of variadic, where it packs the elements into
//    an array, vs. using the va_arg() stack.  So vaptr is nullptr to signal
//    the `p` pointer is this packed array, vs. the first item of a va_list.)
//
Array(*) Scan_UTF8_Managed(
    String(const*) file,
    const Byte* utf8,
    Size size,
    option(Context(*)) context
){
    assert(utf8[size] == '\0');
    UNUSED(size);  // scanner stops at `\0` (no size limit functionality)

    const void* packed[2] = {utf8, rebEND};  // BEWARE: Stack, can't trampoline!
    Feed(*) feed = Make_Variadic_Feed(  // scanner requires variadic, see [1]
        packed, nullptr,  // va_list* as nullptr means `p` is packed, see [2]
        context,
        FEED_MASK_DEFAULT
    );
    Sync_Feed_At_Cell_Or_End_May_Fail(feed);

    StackIndex base = TOP_INDEX;
    while (Not_Feed_At_End(feed)) {
        Derelativize(PUSH(), At_Feed(feed), FEED_SPECIFIER(feed));
        Set_Cell_Flag(TOP, UNEVALUATED);
        Fetch_Next_In_Feed(feed);
    }
    // Note: exhausting feed should take care of the va_end()

    Flags flags = NODE_FLAG_MANAGED;
/*    if (Get_Executor_Flag(SCAN, f, NEWLINE_PENDING))  // !!! feed flag
        flags |= ARRAY_FLAG_NEWLINE_AT_TAIL; */

    Free_Feed(feed);  // feeds are dynamically allocated and must be freed

    Array(*) a = Pop_Stack_Values_Core(base, flags);

    a->misc.line = 1;
    mutable_LINK(Filename, a) = file;
    Set_Subclass_Flag(ARRAY, a, HAS_FILE_LINE_UNMASKED);
    SET_SERIES_FLAG(a, LINK_NODE_NEEDS_MARK);

    return a;
}


//
//  Startup_Scanner: C
//
void Startup_Scanner(void)
{
    REBLEN n = 0;
    while (Token_Names[n])
        ++n;
    assert(cast(enum Reb_Token, n) == TOKEN_MAX);
}


//
//  Shutdown_Scanner: C
//
void Shutdown_Scanner(void)
{
}


//
//  transcode: native [
//
//  {Translates UTF-8 source (from a text or binary) to values}
//
//      return: "Transcoded value (or block of values)"
//          [<opt> any-value!]
//      @next "Translate one value and give back next position"
//          [text! binary!]
//      source "If BINARY!, must be UTF-8 encoded"
//          [text! binary!]
//      /file "File to be associated with BLOCK!s and GROUP!s in source"
//          [file! url!]
//      /line "Line number for start of scan, word variable will be updated"
//          [integer! any-word!]
//      /where "Where you want to bind words to (default unbound)"
//          [module!]
//  ]
//
DECLARE_NATIVE(transcode)
{
    INCLUDE_PARAMS_OF_TRANSCODE;

    REBVAL *source = ARG(source);

    Size size;
    const Byte* bp = VAL_BYTES_AT(&size, source);

    SCAN_STATE *ss;
    REBVAL *ss_buffer = ARG(return);  // kept as a BINARY!, gets GC'd

    enum {
        ST_TRANSCODE_INITIAL_ENTRY = 0,
        ST_TRANSCODE_SCANNING
    };

    switch (STATE) {
      case ST_TRANSCODE_INITIAL_ENTRY :
        goto initial_entry;

      case ST_TRANSCODE_SCANNING :
        ss = cast(SCAN_STATE*, BIN_HEAD(VAL_BINARY_KNOWN_MUTABLE(ss_buffer)));
        goto scan_to_stack_maybe_failed;
    }

  initial_entry: {  //////////////////////////////////////////////////////////

  // 1. Though all BINARY! leave a spare byte at the end in case they are
  //    turned into a string, they are not terminated by default.  (Read about
  //    BINARY_BAD_UTF8_TAIL_BYTE for why; it helps reinforce the fact that
  //    binaries consider 0 a legal content value, while strings do not.)
  //
  //    Most of the time this is a good thing because it helps make sure that
  //    people are passing around the `size` correctly.  But R3-Alpha's
  //    scanner was not written to test against a limit...it looks for `\0`
  //    bytes, so all input must have it.
  //
  //    Hack around the problem by forcing termination on the binary (there
  //    is always room to do this, in case it becomes string-aliased.)
  //
  // 2. Originally, interning was used on the file to avoid redundancy.  But
  //    that meant the interning mechanic was being given strings that were
  //    not necessarily valid WORD! symbols.  There's probably not *that* much
  //    redundancy of files being scanned, and plain old freezing can keep the
  //    user from changing the passed in filename after-the-fact (making a
  //    copy would likely be wasteful, so let them copy if they care to change
  //    the string later).
  //
  //    !!! Should the base name and extension be stored, or whole path?

    if (IS_BINARY(source))  // scanner needs data to end in '\0', see [1]
        TERM_BIN(m_cast(Binary(*), VAL_BINARY(source)));

    String(const*) file;
    if (REF(file)) {
        file = VAL_STRING(ARG(file));
        Freeze_Series(file);  // freezes vs. interning, see [2]
    }
    else
        file = ANONYMOUS;

    const REBVAL *line_number;
    if (ANY_WORD(ARG(line)))
        line_number = Lookup_Word_May_Fail(ARG(line), SPECIFIED);
    else
        line_number = ARG(line);

    REBLIN start_line;
    if (Is_Nulled(line_number)) {
        start_line = 1;
    }
    else if (IS_INTEGER(line_number)) {
        start_line = VAL_INT32(line_number);
        if (start_line <= 0)
            fail (PARAM(line));  // definitional?
    }
    else
        fail ("/LINE must be an INTEGER! or an ANY-WORD! integer variable");

    // Because we're building a frame, we can't make a {bp, END} packed array
    // and start up a variadic feed...because the stack variable would go
    // bad as soon as we yielded to the trampoline.  Have to use an END feed
    // and preload the ss->begin of the scanner here.
    //
    // Note: Could reuse global TG_End_Feed if context was null.

    Feed(*) feed = Make_Array_Feed_Core(EMPTY_ARRAY, 0, SPECIFIED);
    feed->context = REF(where)
        ? VAL_CONTEXT(ARG(where))
        : cast(Context(*), nullptr);  // C++98 ambiguous w/o cast

    Flags flags =
        FRAME_FLAG_TRAMPOLINE_KEEPALIVE  // query pending newline
        | FRAME_FLAG_FAILURE_RESULT_OK  // want to pass on definitional error
        | FRAME_FLAG_ALLOCATED_FEED;
    if (WANTED(next))
        flags |= SCAN_EXECUTOR_FLAG_JUST_ONCE;

    Frame(*) subframe = Make_Frame(feed, flags);
    subframe->executor = &Scanner_Executor;
    SCAN_LEVEL *level = &subframe->u.scan;

    Binary(*) bin = Make_Binary(sizeof(SCAN_STATE));
    ss = cast(SCAN_STATE*, BIN_HEAD(bin));

    UNUSED(size);  // currently we don't use this information

    Init_Scan_Level(level, ss, file, start_line, bp);

    TERM_BIN_LEN(bin, sizeof(SCAN_STATE));

    Init_Binary(ss_buffer, bin);

    Push_Frame(OUT, subframe);
    STATE = ST_TRANSCODE_SCANNING;
    return CONTINUE_SUBFRAME (subframe);

} scan_to_stack_maybe_failed: {  /////////////////////////////////////////////

    // If the source data bytes are "1" then the scanner will push INTEGER! 1
    // if the source data is "[1]" then the scanner will push BLOCK! [1]
    //
    // Return a block of the results, so [1] and [[1]] in those cases.

    if (Is_Raised(OUT)) {
        Drop_Frame(SUBFRAME);
        return OUT;  // the raised error
    }

    if (WANTED(next)) {
        if (TOP_INDEX == STACK_BASE)
            Init_Nulled(OUT);
        else {
            Move_Cell(OUT, TOP);
            DROP();
        }
    }
    else {
        Flags flags = NODE_FLAG_MANAGED;
        if (Get_Executor_Flag(SCAN, SUBFRAME, NEWLINE_PENDING))
            flags |= ARRAY_FLAG_NEWLINE_AT_TAIL;

        Array(*) a = Pop_Stack_Values_Core(STACK_BASE, flags);

        a->misc.line = ss->line;
        mutable_LINK(Filename, a) = ss->file;
        a->leader.bits |= ARRAY_MASK_HAS_FILE_LINE;

        Init_Block(OUT, a);
    }

    Drop_Frame(SUBFRAME);

    if (REF(line) and IS_WORD(ARG(line))) {  // wanted the line number updated
        REBVAL *line_int = ARG(return);  // use return as scratch slot
        Init_Integer(line_int, ss->line);
        if (Set_Var_Core_Throws(SPARE, nullptr, ARG(line), SPECIFIED, line_int))
            return THROWN;
    }

    // Return the input BINARY! or TEXT! advanced by how much the transcode
    // operation consumed.
    //
    if (WANTED(next)) {
        REBVAL *rest = ARG(next);  // use return as scratch slot
        Copy_Cell(rest, source);

        if (IS_BINARY(source)) {
            Binary(const*) bin = VAL_BINARY(source);
            if (ss->begin)
                VAL_INDEX_UNBOUNDED(rest) = ss->begin - BIN_HEAD(bin);
            else
                VAL_INDEX_UNBOUNDED(rest) = BIN_LEN(bin);
        }
        else {
            assert(IS_TEXT(source));

            // !!! The scanner does not currently keep track of how many
            // codepoints it went past, it only advances bytes.  But the TEXT!
            // we're returning here needs a codepoint-based index.
            //
            // Count characters by going backwards from the byte position of
            // the finished scan until the byte we started at is found.
            //
            // (It would probably be better if the scanner kept count, though
            // maybe that would make it slower when this isn't needed?)
            //
            if (ss->begin)
                VAL_INDEX_RAW(rest) += Num_Codepoints_For_Bytes(bp, ss->begin);
            else
                VAL_INDEX_RAW(rest) += BIN_TAIL(VAL_STRING(source)) - bp;
        }
    }

    Proxy_Multi_Returns(frame_);
    return OUT;
}}


//
//  Scan_Any_Word: C
//
// Scan word chars and make word symbol for it.
// This method gets exactly the same results as scanner.
// Returns symbol number, or zero for errors.
//
const Byte* Scan_Any_Word(
    REBVAL *out,
    enum Reb_Kind kind,
    const Byte* utf8,
    Size size
) {
    SCAN_STATE ss;
    String(const*) file = ANONYMOUS;
    const REBLIN start_line = 1;

    Frame(*) f = Make_End_Frame(FRAME_MASK_NONE);  // note: no feed `context`
    SCAN_LEVEL *level = &f->u.scan;

    Init_Scan_Level(level, &ss, file, start_line, utf8);

    DECLARE_MOLD (mo);

    Context(*) error;
    enum Reb_Token token = Maybe_Locate_Token_May_Push_Mold(&error, mo, f);
    if (token != TOKEN_WORD)
        return nullptr;

    assert(ss.end >= ss.begin);
    if (size > cast(Size, ss.end - ss.begin))
        return nullptr;  // e.g. `as word! "ab cd"` just sees "ab"

    Init_Any_Word(out, kind, Intern_UTF8_Managed(utf8, size));
    Drop_Mold_If_Pushed(mo);
    Free_Frame_Internal(f);
    return ss.begin;
}


//
//  Scan_Issue: C
//
// Scan an issue word, allowing special characters.
// Returning null should trigger an error in the caller.
//
// Passed in buffer and size does not count the leading `#` so that the code
// can be used to create issues from buffers without it (e.g. TO-HEX).
//
// !!! Since this follows the same rules as FILE!, the code should merge,
// though FILE! will make mutable strings and not have in-cell optimization.
//
const Byte* Scan_Issue(Cell(*) out, const Byte* cp, Size size)
{
    const Byte* bp = cp;

    // !!! ISSUE! loading should use the same escaping as FILE!, and have a
    // pre-counted mold buffer, with UTF-8 validation done on the prescan.
    //
    REBLEN len = 0;

    Size n = size;
    while (n > 0) {
        if (not Is_Continuation_Byte_If_Utf8(*bp))
            ++len;

        // Allows nearly every visible character that isn't a delimiter
        // as a char surrogate, e.g. #\ or #@ are legal, as are #<< and #>>
        //
        switch (GET_LEX_CLASS(*bp)) {
          case LEX_CLASS_DELIMIT:
            switch (GET_LEX_VALUE(*bp)) {
              case LEX_DELIMIT_SLASH:  // internal slashes are legal
              case LEX_DELIMIT_PERIOD:  // internal dots also legal
                break;

              default:
                // ultimately #{...} and #"..." should be "ISSUECHAR!"
                return nullptr;  // other purposes, `#(` `#[`, etc.
            }
            break;

          case LEX_CLASS_WORD:
            if (*bp == '^')
                return nullptr;  // TBD: #^(NN) for light-looking escapes
            break;

          case LEX_CLASS_SPECIAL:  // includes `<` and '>' and `~`
          case LEX_CLASS_NUMBER:
            break;
        }

        ++bp;
        --n;
    }

    // !!! Review UTF-8 Safety, needs to use mold buffer the way TEXT! does
    // to scan the data.
    //
    Init_Issue_Utf8(out, cast(Utf8(const*), cp), size, len);

    return bp;
}


//
//  Try_Scan_Variadic_Feed_Utf8_Managed: C
//
option(Array(*)) Try_Scan_Variadic_Feed_Utf8_Managed(Feed(*) feed)
{
    assert(Detect_Rebol_Pointer(feed->p)  == DETECTED_AS_UTF8);

    Frame(*) f = Make_Frame(feed, FRAME_MASK_NONE);
    f->executor = &Scanner_Executor;

    SCAN_LEVEL *level = &f->u.scan;
    SCAN_STATE ss;
    const REBLIN start_line = 1;
    Init_Scan_Level(
        level,
        &ss,
        Intern_Unsized_Managed("-variadic-"),
        start_line,
        nullptr  // let scanner fetch feed->p Utf8 as new ss->begin
    );

    DECLARE_LOCAL (temp);
    Push_Frame(temp, f);
    if (Trampoline_With_Top_As_Root_Throws())
        fail (Error_No_Catch_For_Throw(f));

    if (TOP_INDEX == f->baseline.stack_base) {
        Drop_Frame(f);
        return nullptr;
    }

    Flags flags = SERIES_FLAG_MANAGED;
    Array(*) reified = Pop_Stack_Values_Core(f->baseline.stack_base, flags);
    Drop_Frame(f);
    return reified;
}
