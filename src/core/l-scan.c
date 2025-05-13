//
//  file: %l-scan.c
//  summary: "lexical analyzer for source to binary translation"
//  section: lexical
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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



static bool Is_Interstitial_Scan(ScanState* S)
  { return S->mode == '.' or S->mode == '/'; }

INLINE bool Is_Lex_Interstitial(Byte b)
  { return b == '/' or b == '.' or b == ':'; }

INLINE bool Is_Lex_End_List(Byte b)
  { return b == ']' or b == ')'; }


//
// Maps each character to its lexical attributes, using
// a frequency optimized encoding.
//
// UTF8: The values C0, C1, F5 to FF never appear.
//
const Byte g_lex_map[256] =
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
    /* 3A :   */    LEX_DELIMIT|LEX_DELIMIT_COLON,
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
    /* 7C |   */    LEX_WORD,
    /* 7D }   */    LEX_DELIMIT|LEX_DELIMIT_RIGHT_BRACE,
    /* 7E ~   */    LEX_WORD, // !!! once belonged to LEX_SPECIAL
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


//
// Maps each character to its lower case value.  Done this
// way for speed.  Note the odd cases in last block.
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
    Error* error,
    TranscodeState* ss,
    REBLEN line,
    const Byte *line_head
){
    // Skip indentation (don't include in the NEAR)
    //
    const Byte *cp = line_head;
    while (Is_Lex_Space(*cp))
        ++cp;

    // Find end of line to capture in error message
    //
    REBLEN len = 0;
    const Byte *bp = cp;
    while (!ANY_CR_LF_END(*cp)) {
        cp++;
        len++;
    }

    // Put the line count and the line's text into a string.
    //
    // !!! This should likely be separated into an integer and a string, so
    // that those processing the error don't have to parse it back out.
    //
    DECLARE_MOLDER (mo);
    Push_Mold(mo);
    Append_Unencoded(mo->utf8flex, "(line ");
    Append_Int(mo->utf8flex, line);
    Append_Unencoded(mo->utf8flex, ") ");
    Append_Utf8_Utf8(mo->utf8flex, cs_cast(bp), len);

    ERROR_VARS *vars = ERR_VARS(error);
    Init_Text(&vars->nearest, Pop_Molded_String(mo));

    if (ss->file)
        Init_File(&vars->file, unwrap ss->file);
    else
        Init_Nulled(&vars->file);
}


//
//  Error_Syntax: C
//
// Catch-all scanner error handler.  Reports the name of the token that gives
// the complaint, and gives the substring of the token's text.  Populates
// the NEAR field of the error with the "current" line number and line text,
// e.g. where the end point of the token is seen.
//
static Error* Error_Syntax(ScanState* S, Token token) {
    assert(S->begin and not Is_Pointer_Corrupt_Debug(S->begin));
    assert(S->end and not Is_Pointer_Corrupt_Debug(S->end));
    assert(S->end >= S->begin);

    DECLARE_VALUE (token_name);
    Init_Text(
        token_name,
        Make_String_UTF8(Token_Names[token])
    );

    DECLARE_VALUE (token_text);
    Init_Text(
        token_text,
        Make_Sized_String_UTF8(
            cs_cast(S->begin), cast(REBLEN, S->end - S->begin)
        )
    );
    return Error_Scan_Invalid_Raw(token_name, token_text);
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
// We have two options of where to implicate the error...either the start
// of the thing being scanned, or where we are now (or, both).  But we
// only have the start line information for GROUP! and BLOCK!...strings
// don't cause recursions.  So using a start line on a string would point
// at the block the string is in, which isn't as useful.
//
static Error* Error_Missing(ScanState* S, Byte wanted) {
    DECLARE_ELEMENT (expected);
    Init_Text(expected, Make_Codepoint_String(wanted));

    Error* error = Error_Scan_Missing_Raw(expected);

    if (Is_Lex_End_List(wanted))
        Update_Error_Near_For_Line(
            error,
            S->ss,
            S->start_line,
            S->start_line_head
        );
    else
        Update_Error_Near_For_Line(
            error,
            S->ss,
            S->ss->line,
            S->ss->line_head
        );
    return error;
}


//
//  Error_Extra: C
//
// For instance, `load "abc ]"`
//
static Error* Error_Extra(char seen) {
    DECLARE_VALUE (unexpected);
    Init_Text(unexpected, Make_Codepoint_String(seen));
    return Error_Scan_Extra_Raw(unexpected);
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
static Error* Error_Mismatch(ScanState* S, char wanted, char seen) {
    Error* error = Error_Scan_Mismatch_Raw(rebChar(wanted), rebChar(seen));
    Update_Error_Near_For_Line(error, S->ss, S->start_line, S->start_line_head);
    return error;
}


// Conveying the part of a string which contains a CR byte is helpful.  But
// we may see this CR during a scan...e.g. the bytes that come after it have
// not been checked to see if they are valid UTF-8.  We assume all the bytes
// *prior* are known to be valid.
//
INLINE Error* Error_Illegal_Cr(const Byte* at, const Byte* start)
{
    UNUSED(at);
    UNUSED(start);
    return Error_User("Illegal CR");
}


//
//  Try_Scan_UTF8_Char_Escapable: C
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
static Option(const Byte*) Try_Scan_UTF8_Char_Escapable(
    Ucs2Unit* out,
    const Byte *bp
){
    const Byte *cp;
    Byte c;
    Byte lex;

    c = *bp;

    // Handle unicoded char:
    if (c >= 0x80) {
        if (!(bp = Back_Scan_UTF8_Char(out, bp, nullptr))) return nullptr;
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
        while ((lex = g_lex_map[*cp]) > LEX_WORD) {
            c = lex & LEX_VALUE;
            if (c == 0 and lex < LEX_NUMBER)
                break;
            *out = (*out << 4) + c;
            cp++;
        }
        if ((cp - bp) > 4) return nullptr;
        if (*cp == ')') {
            cp++;
            return cp;
        }

        // Check for identifiers:
        for (c = 0; c < ESC_MAX; c++) {
            if ((cp = Match_Bytes(bp, cb_cast(Esc_Names[c])))) {
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


// For compatibility to copy code to and from the bootstrap EXE, this code
// uses Mold_Buffer(mo) to abstract the difference from `mo->utf8flex`.
// (Calling the bootstrap exe's mold buffer a "string" would be potentially
// confusing since "String" doesn't use UTF-8 internally in that codebase.)
//
#define Mold_Buffer(mo) mo->utf8flex


//
//  Trap_Scan_String_Push_Mold: C
//
// Scan a quoted or braced string, handling all the escape characters.  e.g.
// an input stream might have "a^(1234)b" and need to turn "^(1234)" into the
// right UTF-8 bytes for that codepoint in the string.
//
// 1. Historically CR LF was scanned as just an LF.  While a tolerant mode of
//    the scanner might be created someday, for the moment we are being more
//    prescriptive about it by default.
//
// 2. The '\0' codepoint is not legal in ANY-STRING!.  Among the many reasons
//    to disallow it is that APIs like rebSpell() for getting string data
//    return only a pointer--not a pointer and a size, so clients must assume
//    that '\0' is the termination.  With UTF-8 everywhere, Ren-C has made it
//    as easy as possible to work with BINARY! using string-based routines
//    like FIND, etc., so use BINARY! if you need UTF-8 with '\0' in it.
//
static Option(Error*) Trap_Scan_String_Push_Mold(
    const Byte** out,
    Molder* mo,
    const Byte* bp,
    Count dashes,
    ScanState* S  // used for errors
){
    StackIndex base = TOP_INDEX;  // accrue nest counts on stack

    Byte left = *bp;
    Byte right;
    switch (left) {
      case '{':
        right = '}';
        break;

      case '"':
        right = '"';
        break;

      case '[':
        right = ']';
        break;

      case '<':
        right = '>';
        break;

      default:
        assert(false);
        right = '\0';  // satisfy compiler warning
        break;
    }

    Push_Mold(mo);
    const Byte* cp = bp;

    Init_Integer(PUSH(), dashes);  // so nest code is uniform

    ++cp;

    while (true) {  // keep going until nesting levels all closed
        Ucs2Unit c = *cp;

        if (c == right) {  // potentially closes last nest level
            ++cp;
            Count count = 0;
            while (*cp == '-') {
                ++count;
                ++cp;
            }
            if (count > VAL_INT32(TOP))
                return Error_User("Nested -- level closure too long");
            if (count == VAL_INT32(TOP)) {
                DROP();
                if (TOP_INDEX == base)
                    goto finished;  // end overall scan, don't add codepoints
            }

            Append_Codepoint(Mold_Buffer(mo), right);

            for (; count != 0; --count)
                Append_Codepoint(Mold_Buffer(mo), '-');
            continue;  // codepoints were appended already
        }

        if (c == left and dashes == 0 and left == '{') {  // {a {b} c}
            Init_Integer(PUSH(), 0);
            Append_Codepoint(Mold_Buffer(mo), left);
            ++cp;
            continue;
        }

        switch (c) {
          case '\0': {
            return Error_Missing(S, right); }

          case '^':
            if (not (cp = maybe Try_Scan_UTF8_Char_Escapable(&c, cp)))
                return Error_User("Bad character literal in string");
            --cp;  // unlike Back_Scan_XXX, no compensation for ++cp later
            break;

          case '-': {  // look for nesting levels -{a --{b}-- c}- is one string
            Count count = 1;
            Append_Codepoint(Mold_Buffer(mo), '-');
            ++cp;
            while (*cp == '-') {
                ++count;
                Append_Codepoint(Mold_Buffer(mo), '-');
                ++cp;
            }
            if (
                *cp == left
                and VAL_INT32(TOP) != 0  // don't want "--" to nest a scan!
                and count >= VAL_INT32(TOP)
            ){
                Init_Integer(PUSH(), count);
                Append_Codepoint(Mold_Buffer(mo), left);
                ++cp;
            }
            continue; }  // already appended all relevant codepoints

          case CR: {
            enum Reb_Strmode strmode = STRMODE_NO_CR;  // avoid CR [1]
            if (strmode == STRMODE_CRLF_TO_LF) {
                if (cp[1] == LF) {
                    ++cp;
                    c = LF;
                    goto linefeed;
                }
            }
            else
                assert(strmode == STRMODE_NO_CR);
            return (Error_Illegal_Cr(cp, S->begin)); }

          case LF:
          linefeed:
            if (left == '"' and dashes == 0)
                return Error_User("Plain quoted strings not multi-line");
            ++S->ss->line;
            break;

          default:
            if (c >= 0x80) {
                if ((cp = Back_Scan_UTF8_Char(&c, cp, nullptr)) == nullptr)
                    return Error_Bad_Utf8_Raw();
            }
        }

        ++cp;

        if (c == '\0')  // e.g. ^(00) or ^@
            panic (Error_Illegal_Zero_Byte_Raw());  // illegal in strings [2]

        Append_Codepoint(Mold_Buffer(mo), c);
    }

  finished:

    *out = cp;
    return nullptr;  // not an error (success)
}

/*
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
static const Byte *Scan_Quote_Push_Mold(
    Molder* mo,
    const Byte *src,
    TranscodeState* ss
){
    Push_Mold(mo);

    Ucs2Unit term = (*src == '{') ? '}' : '"'; // pick termination
    ++src;

    REBINT nest = 0;
    REBLEN lines = 0;
    while (*src != term or nest > 0) {
        Ucs2Unit chr = *src;

        switch (chr) {

        case 0:
            Term_Binary(mo->utf8flex);
            return nullptr; // Scan_state shows error location.

        case '^':
            if ((src = Scan_UTF8_Char_Escapable(&chr, src)) == nullptr) {
                Term_Binary(mo->utf8flex);
                return nullptr;
            }
            --src;
            break;

        case '{':
            if (term != '"')
                ++nest;
            break;

        case '}':
            if (term != '"' and nest > 0)
                --nest;
            break;

        case CR:
            if (src[1] == LF) src++;
            // fall thru
        case LF:
            if (term == '"') {
                Term_Binary(mo->utf8flex);
                return nullptr;
            }
            lines++;
            chr = LF;
            break;

        default:
            if (chr >= 0x80) {
                if ((src = Back_Scan_UTF8_Char(&chr, src, nullptr)) == nullptr) {
                    Term_Binary(mo->utf8flex);
                    return nullptr;
                }
            }
        }

        src++;

        Append_Codepoint(mo->utf8flex, chr);
    }

    src++; // Skip ending quote or brace.

    ss->line += lines;

    Term_Binary(mo->utf8flex);
    return src;
}
*/


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
const Byte *Scan_Item_Push_Mold(
    Molder* mo,
    const Byte *bp,
    const Byte *ep,
    Byte opt_term, // '\0' if file like %foo - '"' if file like %"foo bar"
    const Byte *opt_invalids
){
    assert(opt_term < 128); // method below doesn't search for high chars

    Push_Mold(mo);

    while (bp < ep and *bp != opt_term) {
        Ucs2Unit c = *bp;

        if (c == '\0')
            break; // End of stream

        if ((opt_term == '\0') and IS_WHITE(c))
            break; // Unless terminator like '"' %"...", any whitespace ends

        if (c < ' ')
            return nullptr; // Ctrl characters not valid in filenames, fail

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
            const bool unicode = false;
            if (!Scan_Hex2(&c, bp + 1, unicode))
                return nullptr;
            bp += 2;
        }
        else if (c == '^') { // Accept ^X encoded char:
            if (bp + 1 == ep)
                return nullptr; // error if nothing follows ^
            if (nullptr == (bp = maybe Try_Scan_UTF8_Char_Escapable(&c, bp)))
                return nullptr;
            if (opt_term == '\0' and IS_WHITE(c))
                break;
            bp--;
        }
        else if (c >= 0x80) { // Accept UTF8 encoded char:
            if (nullptr == (bp = Back_Scan_UTF8_Char(&c, bp, 0)))
                return nullptr;
        }
        else if (opt_invalids and nullptr != strchr(cs_cast(opt_invalids), c)) {
            //
            // Is char as literal valid? (e.g. () [] etc.)
            // Only searches ASCII characters.
            //
            return nullptr;
        }

        ++bp;

        // 4 bytes maximum for UTF-8 encoded character (6 is a lie)
        //
        // https://stackoverflow.com/a/9533324/211160
        //
        if (Flex_Len(mo->utf8flex) + 4 >= Flex_Rest(mo->utf8flex)) // incl term
            Extend_Flex(mo->utf8flex, 4);

        REBLEN encoded_len = Encode_UTF8_Char(Binary_Tail(mo->utf8flex), c);
        Set_Flex_Len(mo->utf8flex, Flex_Len(mo->utf8flex) + encoded_len);
    }

    if (*bp != '\0' and *bp == opt_term)
        ++bp;

    Term_Binary(mo->utf8flex);

    return bp;
}


//
//  Skip_Tag: C
//
// Skip the entire contents of a tag, including quoted strings.
// The argument points to the opening '<'.  nullptr is returned on errors.
//
static const Byte *Skip_Tag(const Byte *cp)
{
    if (*cp == '<')
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
//  Prescan_Token: C
//
// This function updates `S->begin` to skip past leading
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
// the caller will use Get_Lex_Class(S->begin[0]).
// Fingerprinting just helps accelerate further categorization.
//
static REBLEN Prescan_Token(ScanState* S)
{
    TranscodeState* ss = S->ss;

    assert(Is_Pointer_Corrupt_Debug(S->begin));
    assert(Is_Pointer_Corrupt_Debug(S->end));

    const Byte *cp = ss->at;
    REBLEN flags = 0;

    // Skip whitespace (if any) and update the ss
    while (Is_Lex_Space(*cp)) cp++;
    S->begin = cp;

    while (true) {
        switch (Get_Lex_Class(*cp)) {

        case LEX_CLASS_DELIMIT:
            if (cp == S->begin) {
                // Include the delimiter if it is the only character we
                // are returning in the range (leave it out otherwise)
                S->end = cp + 1;

                // Note: We'd liked to have excluded LEX_DELIMIT_END, but
                // would require a Get_Lex_Delimit() call to know to do so.
                // Trap_Locate_Token_May_Push_Mold() does a `switch` on that,
                // so it can subtract this addition back out itself.
            }
            else
                S->end = cp;
            return flags;

        case LEX_CLASS_SPECIAL:
            if (cp != S->begin) {
                // As long as it isn't the first character, we union a flag
                // in the result mask to signal this special char's presence
                Set_Lex_Flag(flags, Get_Lex_Special(*cp));
            }
            cp++;
            break;

        case LEX_CLASS_WORD:
            //
            // If something is in LEX_CLASS_SPECIAL it gets set in the flags
            // that are returned.  But if any member of LEX_CLASS_WORD is
            // found, then a flag will be set indicating that also.
            //
            Set_Lex_Flag(flags, LEX_SPECIAL_WORD);
            while (Is_Lex_Word_Or_Number(*cp)) cp++;
            break;

        case LEX_CLASS_NUMBER:
            while (Is_Lex_Number(*cp)) cp++;
            break;
        }
    }

    DEAD_END;
}


// Make it a little cleaner to return tokens from Trap_Locate_Token, returning
// nullptr for the error, captures the pattern:
//
//    *token = TOKEN_XXX;
//    return nullptr;
//
// as just `return LOCATED(TOKEN_XXX);`
//
#define LOCATED(tok) (*token_out = tok, nullptr)


//
//  Trap_Locate_Token_May_Push_Mold: C
//
// Find the beginning and end character pointers for the next token in the
// scanner state.  If the scanner is being fed variadically by a list of UTF-8
// strings and cell pointers, then any Rebol values encountered will be
// spliced into the array being currently gathered by pushing them to the data
// stack (as tokens can only be located in UTF-8 strings encountered).
//
// The scan state will be updated so that `S->begin` has been moved past any
// leading whitespace that was pending in the buffer.  `S->end` will hold the
// conclusion at a delimiter.  `token_out` will return the calculated token.
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
// Trap_Locate_Token_May_Push_Mold() function.  But if the work of locating the
// value means you have to basically do what you'd do to read it into a cell
// anyway, why split it?  This is especially true now that the variadic
// splicing pushes values directly from this routine.
//
// Error handling is limited for most types, as an additional phase is needed
// to load their data into a REBOL value.  Yet if a "cheap" error is
// incidentally found during this routine without extra cost to compute, it
// can fail here.
//
// Examples with S->(B)egin and S->(E)nd and return value:
//
//     foo baz bar => TOKEN_WORD
//     B  E
//
//     [quick brown fox] => TOKEN_BLOCK_BEGIN
//     B
//      E
//
//     "brown fox]" => TOKEN_WORD
//      B    E
//
//     $10AE.20 sent => panic()
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
static Option(Error*) Trap_Locate_Token_May_Push_Mold(
    Token* token_out,
    Molder* mo,
    ScanState* S
) {
    TranscodeState* ss = S->ss;

  acquisition_loop: {

    Corrupt_Pointer_If_Debug(S->begin);
    Corrupt_Pointer_If_Debug(S->end);

    // If a non-variadic scan of a UTF-8 string is being done, then ss->vaptr
    // will be nullptr and ss->at will be set to the data to scan.  A variadic
    // scan will start ss->at at nullptr also.
    //
    // Each time a string component being scanned gets exhausted, ss->at
    // will be set to nullptr and this loop is run to see if there's more input
    // to be processed.
    //
    while (ss->at == nullptr) {
        if (ss->vaptr == nullptr)  // not a variadic va_list-based scan...
            return LOCATED(TOKEN_END);  // ...so end of utf-8 input was the end

        const void *p = va_arg(*ss->vaptr, const void*);

        if (not p) { // libRebol representation of ~null~/nullptr

            return Error_User(
                "can't splice null in ANY-LIST!...use rebQ()"
            );

        } else switch (Detect_Rebol_Pointer(p)) {

        case DETECTED_AS_END:
            return LOCATED(TOKEN_END);

        case DETECTED_AS_CELL: {
            const Value* splice = cast(const Value*, p);
            if (Is_Antiform(splice))
                return Error_User(
                    "Use rebQ() as VOID, NULL, and TRASH are illegal in API"
                );

            Copy_Cell(PUSH(), splice);

            if (S->newline_pending) {
                S->newline_pending = false;
                Set_Cell_Flag(TOP, NEWLINE_BEFORE);
            }

            if (S->opts & SCAN_FLAG_LOCK_SCANNED) { // !!! for future use...?
                Flex* locker = nullptr;
                Force_Value_Frozen_Deep(TOP, locker);
            }

            if (Is_Api_Value(splice)) { // moved to TOP, can release *now*
                Array* a = Singular_From_Cell(splice);
                if (Get_Flex_Info(a, API_RELEASE))
                    rebRelease(m_cast(Value*, splice)); // !!! m_cast
            }

            break; } // push values to emit stack until UTF-8 or END

        case DETECTED_AS_STUB: {
            //
            // An "instruction", currently just rebQ().

            Array* instruction = cast(Array*, m_cast(void*, p));
            Value* single = KNOWN(ARR_SINGLE(instruction));

            assert(Any_Metaform(single));

            Copy_Cell(PUSH(), single);

            if (S->newline_pending) {
                Set_Cell_Flag(TOP, NEWLINE_BEFORE);
                S->newline_pending = false;
            }

            if (S->opts & SCAN_FLAG_LOCK_SCANNED) { // !!! for future use...?
                Flex* locker = nullptr;
                Force_Value_Frozen_Deep(TOP, locker);
            }

            Free_Instruction(instruction);
            break; }

        case DETECTED_AS_UTF8: {
            ss->at = cast(const Byte*, p);

            // If we're using a va_list, we start the scan with no C string
            // pointer to serve as the beginning of line for an error message.
            // wing it by just setting the line pointer to whatever the start
            // of the first UTF-8 string fragment we see.
            //
            // !!! A more sophisticated debug mode might "reify" the va_list
            // as a BLOCK! before scanning, which might be able to give more
            // context for the error-causing input.
            //
            if (ss->line_head == nullptr) {
                assert(ss->vaptr != nullptr);
                ss->line_head = ss->at;
            }
            break; } // fallthrough to "ordinary" scanning

        default:
            return Error_User(
                "Scanned pointer not END, Value*, or valid UTF-8 string"
            );
        }
    }
  }

    Token token;

    REBLEN flags = Prescan_Token(S);  // sets S->begin, S->end

    const Byte *cp = S->begin;

    if (*cp == '-') {  // first priority: -{...}- --{...}--
        Count dashes = 1;
        const Byte* dp = cp;
        for (++dp; *dp == '-'; ++dp)
            ++dashes;
        if (*dp == '"' or *dp == '[' or *dp == '<') {
            if (*dp == '<')
                token = TOKEN_TAG;
            else
                token = TOKEN_STRING;
            Option(Error*) error = Trap_Scan_String_Push_Mold(
                &cp, mo, dp, dashes, S
            );
            if (error)
                return error;
            goto check_str;
        }
        if (*dp == '{')
            return Error_User("Not supporting --{...}-- in bootstrap yet");
    }

    switch (Get_Lex_Class(*cp)) {

    case LEX_CLASS_DELIMIT:
        switch (Get_Lex_Delimit(*cp)) {
        case LEX_DELIMIT_SPACE:
            crash ("Prescan_Token did not skip whitespace");

        delimit_return:;
        case LEX_DELIMIT_RETURN:
            if (cp[1] == LF)
                ++cp;
            goto delimit_line_feed;

        delimit_line_feed:;
        case LEX_DELIMIT_LINEFEED:
            ss->line++;
            S->end = cp + 1;
            return LOCATED(TOKEN_NEWLINE);

        case LEX_DELIMIT_COMMA:
            S->end = cp + 1;
            return LOCATED(TOKEN_COMMA);

        // [BRACKETS]

        case LEX_DELIMIT_LEFT_BRACKET:
            return LOCATED(TOKEN_BLOCK_BEGIN);

        case LEX_DELIMIT_RIGHT_BRACKET:
            return LOCATED(TOKEN_BLOCK_END);

        // (PARENS)

        case LEX_DELIMIT_LEFT_PAREN:
            return LOCATED(TOKEN_GROUP_BEGIN);

        case LEX_DELIMIT_RIGHT_PAREN:
            return LOCATED(TOKEN_GROUP_END);


        // "QUOTES" and {BRACES}

        case LEX_DELIMIT_DOUBLE_QUOTE: {
            Option(Error*) error = Trap_Scan_String_Push_Mold(
                &cp, mo, cp, 0, S
            );
            if (error)
                return error;
            token = TOKEN_STRING;
            goto check_str; }

        case LEX_DELIMIT_LEFT_BRACE: {
            Option(Error*) error = Trap_Scan_String_Push_Mold(
                &cp, mo, cp, 0, S
            );
            if (error)
                return error;
            token = TOKEN_STRING;
            goto check_str; }

        check_str:
            assert(token == TOKEN_STRING or token == TOKEN_TAG);
            if (cp) {
                S->end = cp;
                return LOCATED(token);
            }
            // try to recover at next new line...
            cp = S->begin + 1;
            while (not ANY_CR_LF_END(*cp))
                ++cp;
            S->end = cp;
            if (S->begin[0] == '"')
                return Error_Missing(S, '"');
            if (S->begin[0] == '{')
                return Error_Missing(S, '}');
            crash ("Invalid string start delimiter");

        case LEX_DELIMIT_RIGHT_BRACE:
            return Error_Extra('}');


          case LEX_DELIMIT_SLASH:  // a /RUN-style PATH! or /// WORD!
            goto handle_delimit_interstitial;

          case LEX_DELIMIT_COLON:  // a :REFINEMENT-style CHAIN! or ::: WORD!
            goto handle_delimit_interstitial;

          case LEX_DELIMIT_PERIOD:  // a .FIELD-style TUPLE! or ... WORD!
            goto handle_delimit_interstitial;

          handle_delimit_interstitial: {
            Byte which = *cp;
            assert(which == '.' or which == ':' or which == '/');
            do {
                if (
                    Is_Lex_Whitespace(cp[1])
                    or cp[1] == ']'
                    or cp[1] == ')'
                    or (cp[1] != which and Is_Lex_Interstitial(cp[1]))
                ){
                    S->end = cp + 1;
                    if (which == ':' and cp[1] == '/')
                        break;  // load `://` with / being the word
                    if (which == '/' and cp[1] == '.')
                        break;  // load `/.a` with / acting as path
                    return LOCATED(TOKEN_WORD);  // like . or .. or ...
                }
                ++cp;
            } while (*cp == which);

            S->end = S->begin + 1;
            switch (which) {
              case '.': return LOCATED(TOKEN_TUPLE);
              case ':': return LOCATED(TOKEN_CHAIN);
              case '/': return LOCATED(TOKEN_PATH);
              default:
                assert(false);
            }
            crash (nullptr); }


        case LEX_DELIMIT_END:
            //
            // We've reached the end of this string token's content.  By
            // putting a nullptr in S->begin, that cues the acquisition loop
            // to check if there's a variadic pointer in effect to see if
            // there's more content yet to come.
            //
            ss->at = nullptr;
            Corrupt_Pointer_If_Debug(S->end);
            goto acquisition_loop;

        default:
            crash ("Invalid LEX_DELIMIT class");
        }

    case LEX_CLASS_SPECIAL:
        if (Get_Lex_Special(*cp) == LEX_SPECIAL_SEMICOLON) {  // begin comment
            while (not ANY_CR_LF_END(*cp))
                ++cp;
            if (*cp == '\0')
                return LOCATED(TOKEN_END);  // load ";" is [] w/no tail newline
            if (*cp == LF)
                goto delimit_line_feed;
            assert(*cp == CR);
            goto delimit_return;
        }

        if (Has_Lex_Flag(flags, LEX_SPECIAL_AT) and *cp != '<') {
            token = TOKEN_EMAIL;
            goto prescan_subsume_all_dots;
        }
    next_ls:
        switch (Get_Lex_Special(*cp)) {

        case LEX_SPECIAL_AT:
            return Error_Syntax(S, TOKEN_EMAIL);

        case LEX_SPECIAL_PERCENT:       /* %filename */
            cp = S->end;
            if (*cp == '"') {
                Option(Error*) e = Trap_Scan_String_Push_Mold(
                    &cp, mo, cp, 0, S
                );
                if (e)
                    return e;
                S->end = cp;
                return LOCATED(TOKEN_FILE);
            }
            while (*cp == '/' or *cp == '.') {  /* deal with delimiter */
                cp++;
                while (Is_Lex_Not_Delimit(*cp))
                    ++cp;
            }
            S->end = cp;
            return LOCATED(TOKEN_FILE);

        case LEX_SPECIAL_APOSTROPHE:
            while (*cp == '\'')  // get sequential apostrophes as one token
                ++cp;
            S->end = cp;
            return LOCATED(TOKEN_APOSTROPHE);

        case LEX_SPECIAL_GREATER:
            if (Is_Lex_Delimit(cp[1]))
                return LOCATED(TOKEN_WORD);
            if (cp[1] == '>') {
                if (Is_Lex_Delimit(cp[2]))
                    return LOCATED(TOKEN_WORD);
                return Error_Syntax(S, TOKEN_WORD);
            }
            // falls through
        case LEX_SPECIAL_LESSER: {
            if (Is_Lex_Whitespace(cp[1]) or cp[1] == ']' or cp[1] == ')' or cp[1] == 0)
                return LOCATED(TOKEN_WORD);  // changed for </tag>
            if (
                (cp[0] == '<' and cp[1] == '<') or cp[1] == '=' or cp[1] == '>'
            ){
                if (Is_Lex_Delimit(cp[2]))
                    return LOCATED(TOKEN_WORD);
                return Error_Syntax(S, TOKEN_WORD);
            }
            if (
                cp[0] == '<' and (cp[1] == '-' or cp[1] == '|')
                and (Is_Lex_Delimit(cp[2]) or Is_Lex_Whitespace(cp[2]))
            ){
                return LOCATED(TOKEN_WORD); // "<|" and "<-"
            }
            if (
                cp[0] == '>' and (cp[1] == '-' or cp[1] == '|')
                and (Is_Lex_Delimit(cp[2]) or Is_Lex_Whitespace(cp[2]))
            ){
                return LOCATED(TOKEN_WORD); // ">|" and ">-"
            }
            if (Get_Lex_Special(*cp) == LEX_SPECIAL_GREATER)
                return Error_Syntax(S, TOKEN_WORD);

            Count dashes = 0;
            Option(Error*) error = Trap_Scan_String_Push_Mold(
                &cp, mo, S->begin, dashes, S
            );
            if (error)
                return error;
            S->end = cp;
            return LOCATED(TOKEN_TAG); }

        case LEX_SPECIAL_PLUS:          /* +123 +123.45 */
        case LEX_SPECIAL_MINUS:         /* -123 -123.45 */
            if (Has_Lex_Flag(flags, LEX_SPECIAL_AT)) {
                token = TOKEN_EMAIL;
                goto prescan_subsume_all_dots;
            }
            cp++;
            if (Is_Lex_Number(*cp)) {
                if (*S->end == ':') {  // thinks it was "delimited" by colon
                    cp = S->end;
                    token = TOKEN_TIME;
                    goto prescan_subsume_up_to_one_dot;  // -596523:14:07.9999
                }
                goto num;  // -123
            }

            if (Is_Lex_Special(*cp)) {
                if ((Get_Lex_Special(*cp)) >= LEX_SPECIAL_POUND)
                    goto next_ls;
                if (*cp == '+' or *cp == '-') {
                    token = TOKEN_WORD;
                    goto scanword;
                }
                if (
                    *cp == '>'
                    and (Is_Lex_Delimit(cp[1]) or Is_Lex_Whitespace(cp[1]))
                ){
                    return LOCATED(TOKEN_WORD);  // Special exemption for ->
                }
                return Error_Syntax(S, TOKEN_WORD);
            }
            token = TOKEN_WORD;
            goto scanword;

        case LEX_SPECIAL_BLANK:
            //
            // `_` standalone should become a BLANK!, so if followed by a
            // delimiter or space.  However `_a_` and `a_b` are left as
            // legal words (at least for the time being).
            //
            if (Is_Lex_Delimit(cp[1]) or Is_Lex_Whitespace(cp[1]))
                return LOCATED(TOKEN_BLANK);
            token = TOKEN_WORD;
            goto scanword;

        case LEX_SPECIAL_POUND:
        pound:
            cp++;
            if (*cp == '[') {
                S->end = ++cp;
                return LOCATED(TOKEN_CONSTRUCT);
            }
            if (*cp == '"') { /* CHAR #"C" */
                Ucs2Unit dummy;
                cp++;
                cp = maybe Try_Scan_UTF8_Char_Escapable(&dummy, cp);
                if (cp != nullptr and *cp == '"') {
                    S->end = cp + 1;
                    return LOCATED(TOKEN_CHAR);
                }
                // try to recover at next new line...
                cp = S->begin + 1;
                while (not ANY_CR_LF_END(*cp))
                    ++cp;
                S->end = cp;
                return Error_Syntax(S, TOKEN_CHAR);
            }
            if (*cp == '{') { /* BINARY #{12343132023902902302938290382} */
                S->end = S->begin;  /* save start */
                S->begin = cp;
                Option(Error*) e = Trap_Scan_String_Push_Mold(
                    &cp, mo, cp, 0, S
                );
                if (e)
                    return e;
                S->begin = S->end;  // restore start
                if (cp) {
                    S->end = cp;
                    return LOCATED(TOKEN_BINARY);
                }
                // try to recover at next new line...
                cp = S->begin + 1;
                while (not ANY_CR_LF_END(*cp))
                    ++cp;
                S->end = cp;
                return Error_Syntax(S, TOKEN_BINARY);
            }
            if (cp - 1 == S->begin)
                return LOCATED(TOKEN_ISSUE);

            return Error_Syntax(S, TOKEN_INTEGER);

        case LEX_SPECIAL_DOLLAR:
            if (  // $10 and $-10 are MONEY!, $a and $-- are "quoted words"
                (cp[1] == '-' and Get_Lex_Class(cp[2]) != LEX_CLASS_NUMBER)
                or (cp[1] != '-' and Get_Lex_Class(cp[1]) != LEX_CLASS_NUMBER)
            ){
                // In the bootstrap process, (get 'x) won't work because X
                // will be unbound.  Allow (get $x) to act like (get 'x) so
                // when the code is run in a new executable it will be bound.
                //
                S->end = cp + 1;
                return LOCATED(TOKEN_APOSTROPHE);
            }
            if (Has_Lex_Flag(flags, LEX_SPECIAL_AT)) {
                token = TOKEN_EMAIL;
                goto prescan_subsume_all_dots;
            }
            token = TOKEN_MONEY;
            goto prescan_subsume_up_to_one_dot;

        default:
            return Error_Syntax(S, TOKEN_WORD);
        }

    case LEX_CLASS_WORD:
        if (*cp == '~' and cp[1] == '<') {  // ~<it's a tripwire...>~
            cp = Skip_Tag(cp);
            if (cp == nullptr)
                return Error_Syntax(S, TOKEN_TRIPWIRE);
            assert(cp[-1] == '>');
            if (*cp != '~')
                return Error_Syntax(S, TOKEN_TRIPWIRE);
            S->end = cp + 1;
            return LOCATED(TOKEN_TRIPWIRE);
        }

        if (
            *S->end == '.'
            and S->mode == '/'
            and not Is_Blank(TOP)  // want /a.b: to be a/b:
            and not (flags & LEX_FLAGS_NONWORD_SPECIALS)

        ){
            token = TOKEN_WORD;
            goto prescan_subsume_all_dots;
        }
        if (
            Only_Lex_Flag(flags, LEX_SPECIAL_WORD)
            and *S->end != ':'  // need additional scan for URL if word://
        ){
            return LOCATED(TOKEN_WORD);
        }
        token = TOKEN_WORD;
        goto scanword;

    case LEX_CLASS_NUMBER:      /* order of tests is important */
      num:;
        if (Has_Lex_Flag(flags, LEX_SPECIAL_AT)) {
            token = TOKEN_EMAIL;
            goto prescan_subsume_all_dots;  // `123@example.com`
        }

        if (*S->end == ':') {  // special interpretation for 10:00 etc
            if (not Is_Lex_Number(S->end[1]))  // but not special for `a.1:`
                return LOCATED(TOKEN_INTEGER);
            token = TOKEN_TIME;
            goto prescan_subsume_up_to_one_dot;
        }

        if (*S->end == '.') {  // special interpretation for 1.2 etc
            if (not Is_Lex_Number(S->end[1]))  // but not special for `1.a`
                return LOCATED(TOKEN_INTEGER);
            return LOCATED(TOKEN_INTEGER);  // !!! see TOKEN_INTEGER hack!
        }

        if (flags == 0)
            return LOCATED(TOKEN_INTEGER);  // simple integer e.g. `123`

        if (Has_Lex_Flag(flags, LEX_SPECIAL_POUND)) {
            if (cp == S->begin) { // no +2 +16 +64 allowed
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
                    // very rare
                    cp++;
                    goto pound;
                }
            }
            return Error_Syntax(S, TOKEN_INTEGER);
        }
        if (Has_Lex_Flag(flags, LEX_SPECIAL_POUND)) { // -#123 2#1010
            if (
                Has_Lex_Flags(
                    flags,
                    ~(
                        LEX_FLAG(LEX_SPECIAL_POUND)
                        | LEX_FLAG(LEX_SPECIAL_APOSTROPHE)
                    )
                )
            ){
                return Error_Syntax(S, TOKEN_INTEGER);
            }
            return LOCATED(TOKEN_INTEGER);
        }
        /* Note: cannot detect dates of the form 1/2/1998 because they
        ** may appear within a path, where they are not actually dates!
        ** Special parsing is required at the next level up. */
        for (;cp != S->end; cp++) {
            // what do we hit first? 1-AUG-97 or 123E-4
            if (*cp == '-')
                return LOCATED(TOKEN_DATE);  // 1-2-97 1-jan-97

            if (*cp == 'x' or *cp == 'X')
                return LOCATED(TOKEN_PAIR); // 320x200

            if (*cp == 'E' or *cp == 'e') {
                if (Skip_To_Byte(cp, S->end, 'x'))
                    return LOCATED(TOKEN_PAIR);
                return LOCATED(TOKEN_DECIMAL);  // 123E4
            }
            if (*cp == '%')
                return LOCATED(TOKEN_PERCENT);
        }
        if (Has_Lex_Flag(flags, LEX_SPECIAL_APOSTROPHE)) // 1'200
            return LOCATED(TOKEN_INTEGER);
        return Error_Syntax(S, TOKEN_INTEGER);

    default:
        ; // put crash() after switch, so no cases fall through
    }

    crash ("Invalid LEX class");

  scanword: { /////////////////////////////////////////////////////////////////

   if (*S->end == ':') {  // word:  url:words
        cp = S->end + 1;
        if (*cp != '/')
            return LOCATED(TOKEN_WORD);
        ++cp;  // saw `:/`
        if (*cp != '/')
            return LOCATED(TOKEN_WORD);
        // saw `://`, okay treat as URL, look for its end
        do {
            ++cp;
            while (Is_Lex_Not_Delimit(*cp) or not Is_Lex_Delimit_Hard(*cp))
                ++cp;  // not delimiter, e.g. `http://example.com]` stops it
        } while (Is_Lex_Interstitial(*cp));  // slash, dots, and colons legal
        S->end = cp;
        return LOCATED(TOKEN_URL);
    }
    if (Has_Lex_Flag(flags, LEX_SPECIAL_AT)) {
        token = TOKEN_EMAIL;
        goto prescan_subsume_all_dots;
    }
    if (Has_Lex_Flag(flags, LEX_SPECIAL_DOLLAR)) {
        token = TOKEN_MONEY;
        goto prescan_subsume_up_to_one_dot;
    }
    if (Has_Lex_Flags(flags, LEX_FLAGS_NONWORD_SPECIALS))  // like \ or %
        return Error_Syntax(S, TOKEN_WORD);

    if (Has_Lex_Flag(flags, LEX_SPECIAL_LESSER)) {
        // Allow word<tag> and word</tag> but not word< word<= word<> etc.

        if (*cp == '=' and cp[1] == '<' and Is_Lex_Delimit(cp[2]))
            return LOCATED(TOKEN_WORD);  // enable `=<`

        cp = Skip_To_Byte(cp, S->end, '<');
        if (
            cp[1] == '<' or cp[1] == '>' or cp[1] == '='
            or Is_Lex_Space(cp[1])
            or (cp[1] != '/' and Is_Lex_Delimit(cp[1]))
        ){
            return Error_Syntax(S, TOKEN_WORD);
        }
        S->end = cp;
    }
    else if (Has_Lex_Flag(flags, LEX_SPECIAL_GREATER)) {
        if ((*cp == '=' or *cp == '|') and cp[1] == '>' and Is_Lex_Delimit(cp[2])) {
            return LOCATED(TOKEN_WORD);  // enable `=>`
        }
        return Error_Syntax(S, TOKEN_WORD);
    }

    return LOCATED(TOKEN_WORD);

} prescan_subsume_up_to_one_dot: { ///////////////////////////////////////////

    assert(
        token == TOKEN_MONEY
        or token == TOKEN_TIME
    );

    // By default, `.` is a delimiter class which stops token scaning.  So if
    // scanning $-10.20 or $-10.20 or $3.04, there is common code to look
    // past the delimiter hit.  The same applies to times.  (DECIMAL! has
    // its own code)

    bool dot_subsumed = false;
    if (*S->end == '.')
        dot_subsumed = true;
    else if (*S->end != ':' and *S->end != ',')
        return LOCATED(token);

    cp = S->end + 1;
    while (
        *cp == ':'
        or (not dot_subsumed and *cp == '.' and (dot_subsumed = true))
        or (not Is_Lex_Delimit(*cp) and not Is_Lex_Whitespace(*cp))
    ){
        ++cp;
    }
    S->end = cp;

    return LOCATED(token);

} prescan_subsume_all_dots: { ////////////////////////////////////////////////

    assert(token == TOKEN_WORD or token == TOKEN_EMAIL);

    // Similar to the above, email scanning in R3-Alpha relied on the non
    // delimiter status of periods to incorporate them into the EMAIL!.
    // (Unlike FILE! or URL!, it did not already have code for incorporating
    // the otherwise-delimiting `/`)  It may be that since EMAIL! is not
    // legal in PATH! there's no real reason not to allow slashes in it, and
    // it could be based on the same code.
    //
    // (This is just good enough to lets the existing tests work on EMAIL!)

    if (*S->end != '.')
        return LOCATED(token);

    cp = S->end + 1;
    while (
        *cp == '.'
        or (not Is_Lex_Delimit(*cp) and not Is_Lex_Whitespace(*cp))
    ){
        ++cp;
    }
    S->end = cp;

    return LOCATED(token);
}}


//
//  Init_Transcode_Vaptr: C
//
// Initialize a transcode session, using variadic C arguments.
//
void Init_Transcode_Vaptr(
    TranscodeState* transcode,
    Option(String*) file,
    LineNumber line,
    Option(const Byte*) begin,  // preload the scanner outside the va_list
    va_list *vaptr
){
    transcode->vaptr = vaptr;

    transcode->at = unwrap begin;  // if null, first fetch from vaptr

    // !!! Splicing REBVALs into a scan as it goes creates complexities for
    // error messages based on line numbers.  Fortunately the splice of a
    // Value* itself shouldn't cause a panic()-class error if there's no
    // data corruption, so it should be able to pick up *a* line head before
    // any errors occur...it just might not give the whole picture when used
    // to offer an error message of what's happening with the spliced values.
    //
    transcode->line_head = nullptr;
    transcode->line = line;

    if (file)
        assert(Is_Flex_Ucs2(unwrap file));
    transcode->file = file;

    transcode->binder = nullptr;
}


//
//  Init_Transcode: C
//
// Initialize a transcode session, using a plain UTF-8 byte argument.
//
void Init_Transcode(
    TranscodeState* transcode,
    Option(String*) file,
    LineNumber line,
    const Byte *utf8,
    REBLEN limit
){
    // The limit feature was not actually supported...just check to make sure
    // it's NUL terminated.
    //
    assert(utf8[limit] == '\0');
    UNUSED(limit);

    transcode->vaptr = nullptr; // signal Locate_Token to not use vaptr
    transcode->at = utf8;

    transcode->line_head = utf8;
    transcode->line = line;

    if (file)
        assert(Is_Flex_Ucs2(unwrap file));
    transcode->file = file;

    transcode->binder = nullptr;
}


//
//  Init_Scan_Level: C
//
// 1. Capture current line and head of line into the starting points, because
//    some errors wish to report the start of the array's location (for
//    instance if you're at the end of the file and you find there is an
//    unmatched open brace, you want to report the start location of the
//    brace...not the end of the file.)
//
void Init_Scan_Level(
    ScanState* S,
    Flags opts,
    TranscodeState* ss,
    Byte mode
){
    S->opts = opts;
    S->ss = ss;
    S->mode = mode;
    assert(
        S->mode == '\0'
        or S->mode == '.' or S->mode == '/'
        or S->mode == ']' or S->mode == ')'
    );

    S->start_line = ss->line;  // capture for error messages [1]
    S->start_line_head = ss->line_head;

    S->newline_pending = false;
    S->num_quotes_pending = 0;
    S->sigil_pending = false;
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
static REBINT Scan_Head(TranscodeState* ss)
{
    const Byte *rp = 0;   /* pts to the REBOL word */
    const Byte *bp = 0;   /* pts to optional [ just before REBOL */
    const Byte *cp = ss->at;
    REBLEN count = ss->line;

    while (true) {
        while (Is_Lex_Space(*cp)) cp++; /* skip white space */
        switch (*cp) {
        case '[':
            if (rp) {
                ss->at = ++cp; //(bp ? bp : cp);
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
            if (*cp == CR and cp[1] == LF) cp++;
            if (*cp) cp++;
            count++;
            break;
        }
    }

    DEAD_END;
}


static Option(Error*) Trap_Scan_Array(Array** out, ScanState* S, Byte mode);


// define for compatibility, adds location to error
//
INLINE Error* Raise_Helper(ScanState* S, const void* p) {
    Drop_Data_Stack_To(S->stack_base);
    Error* e;
    if (Detect_Rebol_Pointer(p) == DETECTED_AS_UTF8)
        e = Error_User(cast(const char*, p));
    else
        e = cast(Error*, m_cast(void*, p));
    Update_Error_Near_For_Line(e, S->ss, S->ss->line, S->ss->line_head);
    return e;
}

#define RAISE(p) Raise_Helper(S,(p))  // capture ss from callsite

//
//  Scan_To_Stack: C
//
// Scans values to the data stack, based on a mode.  This mode can be
// ']', ')', or '/' to indicate the processing type...or '\0'.
//
// If the source bytes are "1" then it will be the array [1]
// If the source bytes are "[1]" then it will be the array [[1]]
//
// Variations like GET-PATH!, SET-PATH! or LIT-PATH! are not discerned in
// the result here.  Instead, ordinary path scanning is done, followed by a
// transformation (e.g. if the first element was a GET-WORD!, change it to
// an ordinary WORD! and make it a GET-PATH!)  The caller does this.
//
// The return value is always nullptr, since output is sent to the data stack.
// (It only has a return value because it may be called by rebRescue(), and
// that's the convention it uses.)
//
Option(Error*) Scan_To_Stack(ScanState* S) {
    TranscodeState* ss = S->ss;

    S->stack_base = TOP_INDEX;  // roll back to here on RAISE()
    assert(not S->newline_pending);
    assert(S->num_quotes_pending == 0);
    assert(not S->sigil_pending);

    DECLARE_MOLDER (mo);

    if (C_STACK_OVERFLOWING(&mo))
        Panic_Stack_Overflow();

    const bool just_once = did (S->opts & SCAN_FLAG_NEXT);
    if (just_once)
        S->opts &= ~SCAN_FLAG_NEXT; // e.g. recursion loads one entire BLOCK!

  loop: { ////////////////////////////////////////////////////////////////////

    Token token;

  {
    Drop_Mold_If_Pushed(mo);
    Option(Error*) error = Trap_Locate_Token_May_Push_Mold(&token, mo, S);
    if (error)
        return RAISE(unwrap error);  // no definitional errors
  }

    if (token == TOKEN_END) {  // reached '\0'
        //
        // At some point, a token for an end of block or group needed to jump
        // to `done`.  If it didn't, we never got a proper closing.
        //
        if (S->mode == ']' or S->mode == ')')
            return RAISE(Error_Missing(S, S->mode));

        goto done;
    }

    assert(S->begin and S->end and S->begin < S->end);

    REBLEN len = cast(REBLEN, S->end - S->begin);

    ss->at = S->end;  // accept token (may be adjusted if token is adjusted)

    switch (token) {
      case TOKEN_NEWLINE:
        S->newline_pending = true;
        ss->line_head = S->end;
        goto loop;

      case TOKEN_COMMA:
        goto loop;

      case TOKEN_BLANK:
        Init_Blank(PUSH());
        break;

      case TOKEN_APOSTROPHE: {  // allows $ for bootstrap
        assert(*S->begin == '\'' or *S->begin == '$');

        if (S->sigil_pending)  // can't do @'foo: or :'foo
            return RAISE(Error_Syntax(S, token));

        if (
            Is_Lex_Whitespace(*S->end)
            or *S->end == ']' or *S->end == ')'
            or *S->end == ';'
        ){
            // !!! Isolated apostrophe is reserved for scanner purposes, most
            // likely for line continuations.
            //
            return RAISE("Illegal isolated quote ' ... may get some purpose");
        }
        else {
            if (len != 1)
                return RAISE(
                    "Old EXE, multiple quoting (e.g. '''x) not supported"
                );
            S->num_quotes_pending = len;  // apply quoting to next token
        }
        goto loop; }

      case TOKEN_WORD: {
        if (len == 0)
            return RAISE(Error_Syntax(S, token));

        Symbol* symbol = Intern_UTF8_Managed(S->begin, len);
        Init_Word(PUSH(), symbol);
        break; }

      case TOKEN_ISSUE:
        if (S->end != Scan_Issue(PUSH(), S->begin + 1, len - 1))
            return RAISE(Error_Syntax(S, token));
        break;

      case TOKEN_BLOCK_BEGIN:
      case TOKEN_GROUP_BEGIN: {
        Array* array;
        Option(Error*) error = Trap_Scan_Array(
            &array, S, (token == TOKEN_BLOCK_BEGIN) ? ']' : ')'
        );
        if (error)
            return RAISE(unwrap error);

        Init_Any_List(
            PUSH(),
            (token == TOKEN_BLOCK_BEGIN) ? TYPE_BLOCK : TYPE_GROUP,
            array
        );
        break; }

      case TOKEN_TUPLE:
        // 1. Internal dots are picked up at the end of scanning each token.
        // This is only for leading periods, which we discard in order
        // to make `.foo` (used in new executables to pick object members)
        // scan as simply `foo`
        //
        assert(*S->begin == '.' and len == 1);
        goto loop;

      case TOKEN_CHAIN:
        //
        // These out-of-turn colons are only used in this bootstrap executable
        // for GET-WORD! and GET-PATH!.
        //
        assert(*S->begin == ':' and len == 1);
        if (S->sigil_pending)
            return RAISE(Error_Syntax(S, TOKEN_CHAIN));
        if (Is_Lex_Interstitial(S->mode))
            return RAISE(Error_Syntax(S, TOKEN_CHAIN));  // foo/:bar illegal
        S->sigil_pending = true;
        goto loop;

      case TOKEN_PATH:
        assert(*S->begin == '/' and len == 1);
        goto out_of_turn_interstitial;

      out_of_turn_interstitial: {
        //
        // A "normal" path or tuple like `a/b/c` or `a.b.c` always has a token
        // on the left of the interstitial.  So the dot or slash gets picked
        // up by a lookahead step after this switch().
        //
        // This point is reached when a slash or dot gets seen "out-of-turn",
        // like `/a` or `a./b` or `~/a` etc.
        //
        // Easiest thing to do here is to push an item and then let whatever
        // processing would happen run (either start a new path or tuple, or
        // continuing one in progress).  So just do that push and "unconsume"
        // the delimiter so the lookahead sees it.

        /* if (level->quasi_pending) {
            Init_Trash(PUSH());  // if we end up with ~/~, we decay it to word
            level->quasi_pending = false;  // quasi-sequences don't exist
        }
        else */
            Init_Blank(PUSH());
        assert(ss->at == S->end);
        ss->at = S->begin;  // "unconsume" .` or `/` or `:` token
        break; }

      case TOKEN_BLOCK_END:
        assert(*S->begin == ']' and len == 1);
        goto handle_list_end_delimiter;

      case TOKEN_GROUP_END:
        assert(*S->begin == ')' and len == 1);
        goto handle_list_end_delimiter;

      handle_list_end_delimiter: {
        Byte end_delimiter = *S->begin;
        if (S->mode == end_delimiter)
            goto done;

        if (Is_Lex_Interstitial(S->mode)) {  // implicit end [the /] (abc/)
            Init_Blank(PUSH());  // add a blank
            assert(ss->at == S->end);  // falsely accepted end_delimiter
            --ss->at;  // unaccept, and end the interstitial scan first
            goto done;
        }

        if (S->mode != '\0')  // expected ']' before ')' or vice-versa
            return RAISE(Error_Mismatch(S, S->mode, end_delimiter));

        return RAISE(Error_Extra(end_delimiter)); }  // stray end delimiter

    // We treat `10.20.30` as a TUPLE!, but `10.20` has a cultural lock on
    // being a DECIMAL! number.  Due to the overlap, Locate_Token() does
    // not have enough information in hand to discern TOKEN_DECIMAL; it
    // just returns TOKEN_INTEGER and the decision is made here.
    //
    // (Imagine we're in a tuple scan and INTEGER! 10 was pushed, and are
    // at "20.30" in the 10.20.30 case.  Locate_Token() would need access
    // to S->mode to know that the tuple scan was happening, else
    // it would have to conclude "20.30" was TOKEN_DECIMAL.  Deeper study
    // would be needed to know if giving Locate_Token() more information
    // is wise.  But that study would likely lead to the conclusion that
    // the whole R3-Alpha scanner concept needs a full rewrite!)
    //
    // Note: We can't merely start with assuming it's a TUPLE!, scan the
    // values, and then decide it's a DECIMAL! when the tuple is popped
    // if it's two INTEGER!.  Because the integer scanning will lose
    // leading digits on the second number (1.002 would become 1.2).

      case TOKEN_INTEGER:     // or start of DATE
        if (
            (*S->end == '.')
            and not Is_Interstitial_Scan(S)  // not in PATH! (yet)
            and Is_Lex_Number(S->end[1])  // If #, we're seeing `###.#???`
        ){
            // If we will be scanning a TUPLE!, then we're at the head of it.
            // But it could also be a DECIMAL! if there aren't any more dots.
            //
            const Byte* temp = S->end + 1;
            REBLEN temp_len = len + 1;
            for (; *temp != '.'; ++temp, ++temp_len) {
                if (Is_Lex_Delimit(*temp)) {  // non-dot delimiter before dot
                    S->end = temp;  // note that S->begin hasn't moved
                    ss->at = S->end;  // accept new token material
                    len = temp_len;
                    goto scan_decimal;
                }
            }
            while (*temp == '.' or not Is_Lex_Delimit(*temp))
                { ++temp; ++temp_len; }

            S->end = S->begin + temp_len;
            if (S->end != Scan_Tuple(PUSH(), S->begin, temp_len))
                return RAISE(Error_Syntax(S, TOKEN_TUPLE));
            ss->at = S->end;  // accept expanded tuple-token
            break;
        }

        // Wasn't beginning of a DECIMAL!, so scan as a normal INTEGER!
        //
        if (S->end != Scan_Integer(PUSH(), S->begin, len))
            return RAISE(Error_Syntax(S, token));
        break;

      scan_decimal:  // we jump here from TOKEN_INTEGER if it expands the token
      case TOKEN_DECIMAL:
      case TOKEN_PERCENT:
        if (*S->end == '/') {  // Do not allow 1.2/abc:
            ++S->end;  // include the slash in the error
            return RAISE(Error_Syntax(S, token));
        }
        if (S->end != Scan_Decimal(PUSH(), S->begin, len, false))
            return RAISE(Error_Syntax(S, token));

        if (S->begin[len - 1] == '%') {
            RESET_CELL(TOP, TYPE_PERCENT);
            VAL_DECIMAL(TOP) /= 100.0;
        }
        break;

      case TOKEN_MONEY:
        if (*S->end == '/') {  // Do not allow $1/$2:
            ++S->end;  // include the slash in the error
            return RAISE(Error_Syntax(S, token));
        }
        if (*S->begin == '-')
            return RAISE(Error_Syntax(S, token));
        if (S->end != Scan_File_Or_Money(PUSH(), S->begin, len))
            return RAISE(Error_Syntax(S, token));
        break;

      case TOKEN_TIME: {
        const Byte* bp = S->begin;
        const Byte* ep = S->end;
        if (
            bp[len - 1] == ':'
            and S->mode == '/'  // could be path/10: set
        ){
            if (ep - 1 != Scan_Integer(PUSH(), bp, len - 1))
                return RAISE(Error_Syntax(S, token));
            S->end--;  // put ':' back on end but not beginning
            break;
        }
        if (ep != Scan_Time(PUSH(), bp, len))
            return RAISE(Error_Syntax(S, token));
        break; }

      case TOKEN_DATE: {
        const Byte* ep = S->end;
        while (*ep == '/' and S->mode != '/') {  // Is date/time?
            ep++;
            while (*ep == '.' or *ep == ':' or Is_Lex_Not_Delimit(*ep))
                ++ep;
            len = ep - S->begin;
            if (len > 50) {
                // prevent infinite loop, should never be longer than this
                break;
            }
            S->end = ep;  // End point extended to cover time
        }
        if (S->end != Scan_Date(PUSH(), S->begin, len))
            return RAISE(Error_Syntax(S, token));
        ss->at = S->end;  // accept the extended token
        break; }

      case TOKEN_CHAR: {
        const Byte* bp = S->begin + 2;  // skip #"
        const Byte* ep = S->end - 1;  // drop "
        if (ep != Try_Scan_UTF8_Char_Escapable(&VAL_CHAR(PUSH()), bp))
            return RAISE(Error_Syntax(S, token));
        RESET_CELL(TOP, TYPE_CHAR);
        break; }

      case TOKEN_STRING: {
        // During scan above, string was stored in MOLD_BUF (UTF-8)
        //
        Flex* s = Pop_Molded_String(mo);
        Init_Text(PUSH(), s);
        break; }

      case TOKEN_BINARY:
        if (S->end != Scan_Binary(PUSH(), S->begin, len))
            return RAISE(Error_Syntax(S, token));
        break;

      case TOKEN_PAIR:
        if (S->end != Scan_Pair(PUSH(), S->begin, len))
            return RAISE(Error_Syntax(S, token));
        break;

      case TOKEN_FILE:
        if (S->end != Scan_File_Or_Money(PUSH(), S->begin, len))
            return RAISE(Error_Syntax(S, token));
        break;

      case TOKEN_EMAIL:
        if (S->end != Scan_Email(PUSH(), S->begin, len))
            return RAISE(Error_Syntax(S, token));
        break;

      case TOKEN_URL:
        if (S->end != Scan_URL(PUSH(), S->begin, len))
            return RAISE(Error_Syntax(S, token));
        break;

      case TOKEN_TAG: {
        // During scan above, string was stored in MOLD_BUF (UTF-8)
        //
        Flex* s = Pop_Molded_String(mo);
        Init_Tag(PUSH(), s);
        break; }

      case TOKEN_TRIPWIRE: {
        // The Scan_Any routine (only used here for tag) doesn't
        // know where the tag ends, so it scans the len.
        //
        const Byte* bp = S->begin + 2;  // skip '~<'
        const Byte* ep = S->end - 2;  // !!! subtract out what ???
        if (ep != Scan_Any(PUSH(), bp, len - 4, TYPE_TRIPWIRE))
            return RAISE(Error_Syntax(S, token));
        break; }

      case TOKEN_CONSTRUCT: {
        Array* array;
        Option(Error*) error = Trap_Scan_Array(&array, S, ']');
        if (error)
            return RAISE(unwrap error);

        if (Array_Len(array) == 0 or not Is_Word(Array_Head(array))) {
            DECLARE_VALUE (temp);
            Init_Block(temp, array);
            return RAISE(Error_Malconstruct_Raw(temp));
        }

        Option(SymId) id = Cell_Word_Id(Array_Head(array));
        if (not id)
            return RAISE(Error_Syntax(S, token));

        if (IS_KIND_SYM(unwrap id)) {
            enum Reb_Kind kind = KIND_FROM_SYM(unwrap id);

            MAKE_HOOK hook = Make_Hooks[kind];

            if (hook == nullptr or Array_Len(array) != 2) {
                DECLARE_VALUE (temp);
                Init_Block(temp, array);
                return RAISE(Error_Malconstruct_Raw(temp));
            }

            // !!! As written today, MAKE may call into the evaluator, and
            // hence a GC may be triggered.  Performing evaluations during
            // the scanner is a questionable idea, but at the very least
            // `array` must be guarded, and a data stack cell can't be
            // used as the destination...because a raw pointer into the
            // data stack could go bad on any DS_PUSH or DROP().
            //
            DECLARE_VALUE (cell);
            Init_Unreadable(cell);
            Push_GC_Guard(cell);

            Push_GC_Guard(array);
            Bounce bounce = hook(cell, kind, KNOWN(Array_At(array, 1)));
            if (bounce == BOUNCE_THROWN) { // !!! good argument against MAKE
                assert(false);
                return RAISE("MAKE during construction syntax threw--illegal");
            }
            if (bounce != cell) { // !!! not yet supported
                assert(false);
                return RAISE("MAKE during construction syntax not out cell");
            }
            Drop_GC_Guard(array);

            Copy_Cell(PUSH(), cell);
            Drop_GC_Guard(cell);
        }
        else {
            DECLARE_VALUE (temp);
            Init_Block(temp, array);
            return RAISE(Error_Malconstruct_Raw(temp));
        }
        break; } // case TOKEN_CONSTRUCT

      case TOKEN_END:
        goto loop;

      default:
        crash ("Invalid TOKEN in Scanner.");
    }

} { //=//// FINISHED SWITCHING ON TOKEN ///////////////////////////////////=//

    // !!! If there is a binder in effect, we also bind the item while
    // we have loaded it.  For now, assume any negative numbers are into
    // the lib context (which we do not expand) and any positive numbers
    // are into the user context (which we will expand).
    //
    if (ss->binder and Any_Word(TOP)) {
        Symbol* canon = VAL_WORD_CANON(TOP);
        REBINT n = Get_Binder_Index_Else_0(ss->binder, canon);
        if (n > 0) {
            //
            // Exists in user context at the given positive index.
            //
            INIT_BINDING(TOP, ss->context);
            INIT_WORD_INDEX(TOP, n);
        }
        else if (MISC(canon).bind_index.lib) {
            //
            // A proxy needs to be imported from lib to context.
            //
            Expand_Context(ss->context, 1);
            Move_Var( // preserve infix state
                Append_Context(ss->context, TOP, nullptr),
                Varlist_Slot(ss->lib, MISC(canon).bind_index.lib)
            );
            Add_Binder_Index(ss->binder, canon, VAL_WORD_INDEX(TOP));
        }
        else {
            // Doesn't exist in either lib or user, create a new binding
            // in user (this is not the preferred behavior for modules
            // and isolation, but going with it for the API for now).
            //
            Expand_Context(ss->context, 1);
            Append_Context(ss->context, TOP, nullptr);
            Add_Binder_Index(ss->binder, canon, VAL_WORD_INDEX(TOP));
        }
    }

    // Check for end of path:
    if (Is_Interstitial_Scan(S)) {
        if (
            *ss->at == ':'  // we want a:b:c -> a/b/c, but a.b: -> a/b:
            and (
                Is_Lex_Whitespace(ss->at[1])
                or ss->at[1] == ')'
                or ss->at[1] == ']'
            )
        ){
            goto done;
        }

        if (*ss->at != '/' and *ss->at != '.' and *ss->at != ':')
            goto done;

        ++ss->at;  // skip next /

        if (
            *ss->at == '\0'
            or Is_Lex_Space(*ss->at)
            or ANY_CR_LF_END(*ss->at)
            or *ss->at == ')' or *ss->at == ']'
        ){
            Init_Blank(PUSH());
            goto done;
        }

        goto loop;
    }
    else if (
        *ss->at == '/' or *ss->at == '.'
        or (
            *ss->at == ':'  // we want a:b:c -> a/b/c, but a.b: -> a/b:
            and not Is_Lex_Whitespace(ss->at[1])
            and ss->at[1] != ')'
            and ss->at[1] != ']'
        )
    ){
        // We're noticing a path was actually starting with the token
        // that just got pushed, so it should be a part of that path.
        //
        // For bootstrap we want `abc.def.ghi` to scan as `abc/def/ghi`
        //
        // 1. A trick that tried to scan `abc/def/ghi.txt` to have a WORD! in
        //    the last position did more harm than good, because `ghi.txt`
        //    still scanned as `ghi/txt`.  So you couldn't compensate by just
        //    translating the last item to an extension for TO FILE!.  Rather
        //    than delete the code which does the accommodation, we just say
        //    all interstitial scans use what was the '.' mode.

        Byte mode = '.';  // if we use mode = '/', then a/b.c => a/b.c [1]
        ++ss->at;

        LineNumber captured_line = ss->line;
        bool captured_newline_pending = false;

        // The way that path scanning works is that after one item has been
        // scanned it is *retroactively* decided to begin picking up more
        // items.  Hence, we take over one pushed item.
        //
        StackIndex base = TOP_INDEX - 1;  // consume item

        if (
            *ss->at == '\0' // `foo/`
            or Is_Lex_Whitespace(*ss->at) // `foo/ bar`
            or *ss->at == ';' // `foo/;--bar`
        ){
            // These are valid paths in modern Ren-C with blanks at their
            // tails, which mean "fetch action but don't run it".  That is
            // useful and better than the old GET-WORD!, so support it!

            Init_Blank(PUSH());
        }
        else {
            // Capture current line and head of line into the starting points,
            // some errors wish to report the start of the array's location.
            //
            ScanState child;
            Init_Scan_Level(
                &child,
                S->opts & (~ SCAN_FLAG_NEXT),
                ss,
                mode
            );

            Option(Error*) error = Scan_To_Stack(&child);
            if (error)
                return RAISE(unwrap error);

            captured_newline_pending = child.newline_pending;
        }

        assert(TOP_INDEX - base >= 2);  // must push at least 2 things

        if (  // look for refinement-style paths [_ word]
            TOP_INDEX - base == 2
            and Is_Blank(TOP - 1)
            and Is_Word(TOP)
        ){
            Copy_Cell(TOP - 1, TOP);
            DROP();
            KIND_BYTE(TOP) = TYPE_REFINEMENT;
            goto finished_path_scan;
        }

        bool leading_blank = Is_Blank(Data_Stack_At(base + 1));
        Array* a = Pop_Stack_Values_Core(
            leading_blank ? base + 1 : base,
            NODE_FLAG_MANAGED
                | (captured_newline_pending ? ARRAY_FLAG_NEWLINE_AT_TAIL : 0)
        );
        if (leading_blank)
            DROP();

        assert(Array_Len(a) >= 2);

        // Tag array with line where the beginning slash was found
        //
        MISC(a).line = captured_line;
        LINK(a).file = maybe ss->file;
        Set_Array_Flag(a, HAS_FILE_LINE);

        assert(not Is_Get_Word(Array_Head(a)));
        RESET_CELL(PUSH(), TYPE_PATH);

        INIT_VAL_ARRAY(TOP, a);
        VAL_INDEX(TOP) = 0;
    }

} finished_path_scan: { ///////////////////////////////////////////////////////

    if (S->opts & SCAN_FLAG_LOCK_SCANNED) { // !!! for future use...?
        Flex* locker = nullptr;
        Force_Value_Frozen_Deep(TOP, locker);
    }

    if (S->sigil_pending) {
        switch (KIND_BYTE(TOP)) {
          case TYPE_WORD:
            KIND_BYTE(TOP) = TYPE_GET_WORD;
            break;
          case TYPE_PATH:
            KIND_BYTE(TOP) = TYPE_GET_PATH;
            break;
          default:
            return RAISE(
                "Old EXE, only TYPE_WORD/TYPE_PATH can be colon-prefixed"
            );
        }
        S->sigil_pending = false;
    }

    if (*ss->at == ':') {
         switch (KIND_BYTE(TOP)) {
          case TYPE_WORD:
          case TYPE_REFINEMENT:  // we want /foo: to be foo: (assigns action)
            KIND_BYTE(TOP) = TYPE_SET_WORD;
            break;
          case TYPE_PATH:
            KIND_BYTE(TOP) = TYPE_SET_PATH;
            break;
          default:
            return RAISE(
                "Old EXE, only TYPE_WORD/TYPE_PATH can be colon-prefixed"
            );
        }
        ++ss->at;
    }

    if (S->num_quotes_pending) {
        assert(S->num_quotes_pending == 1);
        switch (KIND_BYTE(TOP)) {
          case TYPE_WORD:
            KIND_BYTE(TOP) = TYPE_LIT_WORD;
            break;
          case TYPE_PATH:
            KIND_BYTE(TOP) = TYPE_LIT_PATH;
            break;
          case TYPE_BLOCK:
            // we scan '[a b c] as just [a b c]... "compatible enough"
            break;
          default:
            return RAISE(
                "Old EXE, WORD/PATH can be quoted once, BLOCK quote ignored!"
            );
        }
        S->num_quotes_pending = 0;
    }

    // Set the newline on the new value, indicating molding should put a
    // line break *before* this value (needs to be done after recursion to
    // process paths or other arrays...because the newline belongs on the
    // whole array...not the first element of it).
    //
    if (S->newline_pending) {
        Set_Cell_Flag(TOP, NEWLINE_BEFORE);
        S->newline_pending = false;
    }

    // Added for TRANSCODE/NEXT (LOAD/NEXT is deprecated, see #1703)
    //
    if (just_once)
        goto done;

    goto loop;

} done: {  ///////////////////////////////////////////////////////////////////

    Drop_Mold_If_Pushed(mo);

    assert(S->num_quotes_pending == 0);
    assert(not S->sigil_pending);

    // S->newline_pending may be true; used for ARRAY_FLAG_NEWLINE_AT_TAIL

    return nullptr;  // used with rebRescue(), so protocol requires a return
}}


//
//  Trap_Scan_Array: C
//
// This routine would create a new structure on the scanning stack.  Putting
// what would be local variables for each level into a structure helps with
// reflection, allowing for better introspection and error messages.  (This
// is similar to the benefits of LevelStruct.)
//
static Option(Error*) Trap_Scan_Array(Array** out, ScanState* S, Byte mode)
{
    assert(mode == ')' or mode == ']');

    TranscodeState* ss = S->ss;

    ScanState child;
    Init_Scan_Level(
        &child,
        S->opts & (~ SCAN_FLAG_NEXT),
        ss,
        mode
    );

    StackIndex base = TOP_INDEX;

    Option(Error*) error = Scan_To_Stack(&child);
    if (error)
        return error;

    Array* a = Pop_Stack_Values_Core(
        base,
        NODE_FLAG_MANAGED
            | (child.newline_pending ? ARRAY_FLAG_NEWLINE_AT_TAIL : 0)
    );

    // Tag array with line where the beginning bracket/group/etc. was found
    //
    MISC(a).line = ss->line;
    LINK(a).file = maybe ss->file;
    Set_Array_Flag(a, HAS_FILE_LINE);

    *out = a;
    return nullptr;
}


//
//  Scan_UTF8_Managed: C
//
// Scan source code. Scan state initialized. No header required.
//
Array* Scan_UTF8_Managed(
    Option(String*) filename,
    const Byte *utf8,
    REBLEN size
){
    TranscodeState transcode;
    const LineNumber start_line = 1;
    Init_Transcode(&transcode, filename, start_line, utf8, size);

    ScanState scan;
    Init_Scan_Level(&scan, SCAN_MASK_NONE, &transcode, '\0');

    StackIndex base = TOP_INDEX;
    Option(Error*) error = Scan_To_Stack(&scan);
    if (error)
        panic (unwrap error);

    Array* a = Pop_Stack_Values_Core(
        base,
        NODE_FLAG_MANAGED
            | (scan.newline_pending ? ARRAY_FLAG_NEWLINE_AT_TAIL : 0)
    );

    MISC(a).line = transcode.line;
    LINK(a).file = maybe transcode.file;
    Set_Array_Flag(a, HAS_FILE_LINE);

    return a;
}


//
//  Scan_Header: C
//
// Scan for header, return its offset if found or -1 if not.
//
REBINT Scan_Header(const Byte *utf8, REBLEN len)
{
    TranscodeState ss;
    String* filename = nullptr;
    const LineNumber start_line = 1;
    Init_Transcode(&ss, filename, start_line, utf8, len);

    REBINT result = Scan_Head(&ss);
    if (result == 0)
        return -1;

    const Byte *cp = ss.at - 2;

    // Backup to start of it:
    if (result > 0) { // normal header found
        while (cp != utf8 and *cp != 'r' and *cp != 'R')
            --cp;
    } else {
        while (cp != utf8 and *cp != '[')
            --cp;
    }
    return cast(REBINT, cp - utf8);
}


//
//  Startup_Scanner: C
//
void Startup_Scanner(void)
{
    REBLEN n = 0;
    while (Token_Names[n] != nullptr)
        ++n;
    assert(cast(Token, n) == TOKEN_MAX);

    TG_Buf_Ucs2 = Make_String(1020);
}


//
//  Shutdown_Scanner: C
//
void Shutdown_Scanner(void)
{
    Free_Unmanaged_Flex(TG_Buf_Ucs2);
    TG_Buf_Ucs2 = nullptr;
}


//
//  transcode: native [
//
//  {Translates UTF-8 binary source to values.}
//
//      return: [any-value! block! binary! text! error!]
//      source [<opt-out> binary! text!]
//          "Must be Unicode UTF-8 encoded"
//      /next3
//          {Translate next complete value (blocks as single value)}
//          next-arg [any-word!]  ; word to set to transcoded value
//      /one "Return a single value, error if more material than that"
//      /file
//          file-name [file! url!]
//      /line
//          line-number [integer! word!]
//  ]
//
DECLARE_NATIVE(TRANSCODE)
{
    INCLUDE_PARAMS_OF_TRANSCODE;

    // !!! Should the base name and extension be stored, or whole path?
    //
    String* filename = Bool_ARG(FILE)
        ? Cell_String(ARG(FILE_NAME))
        : nullptr;

    LineNumber start_line;
    if (Bool_ARG(LINE)) {
        Value* ival;
        if (Is_Word(ARG(LINE_NUMBER)))  // get mutable, to panic early
            ival = Get_Mutable_Var_May_Panic(ARG(LINE_NUMBER), SPECIFIED);
        else
            ival = ARG(LINE_NUMBER);

        if (not Is_Integer(ival))
            panic (ARG(LINE_NUMBER));

        start_line = VAL_INT32(ival);
        if (start_line <= 0)
            panic (Error_Invalid(ival));
    }
    else
        start_line = 1;

    Value* source = ARG(SOURCE);
    Binary* converted = nullptr;
    if (Is_Text(source)) {
        converted = Make_Utf8_From_Cell_String_At_Limit(
            source, Cell_Series_Len_At(source)
        );
    }

    TranscodeState transcode;
    Init_Transcode(
        &transcode,
        filename,
        start_line,
        converted ? Binary_Head(converted) : Cell_Blob_At(source),
        converted ? Binary_Len(converted) : Cell_Series_Len_At(source)
    );

    ScanState scan;
    Init_Scan_Level(
        &scan,
        Bool_ARG(NEXT3) ? SCAN_FLAG_NEXT : SCAN_MASK_NONE,
        &transcode,
        '\0'
    );

    // If the source data bytes are "1" then the scanner will push INTEGER! 1
    // if the source data is "[1]" then the scanner will push BLOCK! [1]
    //
    // Return a block of the results, so [1] and [[1]] in those cases.
    //
    StackIndex base = TOP_INDEX;

    Option(Error*) error = Scan_To_Stack(&scan);
    if (error) {
        if (converted)
            Free_Unmanaged_Flex(converted);  // release temporary binary
        return Init_Error(OUT, unwrap error);
    }

    if (Is_Word(ARG(LINE_NUMBER))) {
        Value* ivar = Get_Mutable_Var_May_Panic(ARG(LINE_NUMBER), SPECIFIED);
        Init_Integer(ivar, transcode.line);
    }
    if (Bool_ARG(NEXT3) and TOP_INDEX != base) {
        Copy_Cell(OUT, source);  // result will be new position
        if (converted) {
            assert(Is_Text(OUT));  // had to be binary converted
            assert(transcode.at <= Binary_Tail(converted));
            assert(transcode.at >= Binary_Head(converted));
            Byte* bp = Binary_Head(converted);
            for (; bp < transcode.at; ++bp) {
                if (not Is_Continuation_Byte(*bp))
                    ++VAL_INDEX(OUT);  // bump ahead for each utf8 codepoint
            }
        }
        else {
            assert(Is_Binary(OUT));  // was utf-8 data
            VAL_INDEX(OUT) = transcode.at - Cell_Blob_Head(OUT);  // advance
        }
    }

    if (converted)
        Free_Unmanaged_Flex(converted);  // release temporary binary created

    if (Bool_ARG(NEXT3)) {
        Value* nvar = Get_Mutable_Var_May_Panic(ARG(NEXT_ARG), SPECIFIED);

        if (TOP_INDEX == base) {
            Init_Nulled(nvar);  // matches modern Ren-C optional unpack
            return nullptr;
        }

        Copy_Cell(nvar, TOP);
        DROP();
        return OUT;  // position set above
    }

    if (Bool_ARG(ONE)) {
        if (TOP_INDEX == base)
            panic ("TRANSCODE:ONE got zero values");
        if (TOP_INDEX > base + 1)
            panic ("TRANSCODE:ONE got more than one value");
        Copy_Cell(OUT, TOP);
        DROP();
        return OUT;
    }

    Array* a = Pop_Stack_Values_Core(
        base,
        NODE_FLAG_MANAGED
            | (scan.newline_pending ? ARRAY_FLAG_NEWLINE_AT_TAIL : 0)
    );
    MISC(a).line = transcode.line;
    LINK(a).file = maybe transcode.file;
    Set_Array_Flag(a, HAS_FILE_LINE);

    return Init_Block(OUT, a);
}


//
//  Scan_Any_Word: C
//
// Scan word chars and make word symbol for it.
// This method gets exactly the same results as scanner.
// Returns symbol number, or zero for errors.
//
const Byte *Scan_Any_Word(
    Value* out,
    enum Reb_Kind kind,
    const Byte *utf8,
    REBLEN len
) {
    TranscodeState transcode;
    String* filename = nullptr;
    const LineNumber start_line = 1;
    Init_Transcode(&transcode, filename, start_line, utf8, len);

    ScanState scan;
    Init_Scan_Level(&scan, SCAN_MASK_NONE, &transcode, '\0');

    DECLARE_MOLDER (mo);

    Token token;
    Option(Error*) error = Trap_Locate_Token_May_Push_Mold(&token, mo, &scan);
    if (error)
        panic (unwrap error);

    if (token != TOKEN_WORD)
        return nullptr;

    Init_Any_Word(out, kind, Intern_UTF8_Managed(utf8, len));
    Drop_Mold_If_Pushed(mo);
    return transcode.at;  // !!! is this right?
}


//
//  Scan_Issue: C
//
// Scan an issue word, allowing special characters.
//
const Byte *Scan_Issue(Value* out, const Byte *cp, REBLEN len)
{
    if (len == 0) return nullptr; // will trigger error

    while (Is_Lex_Space(*cp)) cp++; /* skip white space */

    const Byte *bp = cp;

    REBLEN l = len;
    while (l > 0) {
        switch (Get_Lex_Class(*bp)) {
          case LEX_CLASS_DELIMIT: {
            LexDelimit ld = Get_Lex_Delimit(*bp);
            if (
                ld == LEX_DELIMIT_PERIOD  // #. is a legal issue
                or ld == LEX_DELIMIT_COLON  // #: is a legal issue
            ){
                goto lex_word_or_number;
            }
            return nullptr; }  // will trigger error

          case LEX_CLASS_SPECIAL: { // Flag all but first special char
            LexSpecial ls = Get_Lex_Special(*bp);
            if (
                LEX_SPECIAL_APOSTROPHE != ls
                and LEX_SPECIAL_PLUS != ls
                and LEX_SPECIAL_MINUS != ls
                and LEX_SPECIAL_BLANK != ls
            ){
                return nullptr; // will trigger error
            }}
            goto lex_word_or_number;

          lex_word_or_number:;
          case LEX_CLASS_WORD:
          case LEX_CLASS_NUMBER:
            bp++;
            l--;
            break;
        }
    }

    Symbol* str = Intern_UTF8_Managed(cp, len);
    Init_Issue(out, str);
    return bp;
}
