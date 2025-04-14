//
//  File: %l-scan.c
//  Summary: "Lexical analyzer for UTF-8 source to Rebol Array translation"
//  Section: lexical
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2024 Ren-C Open Source Contributors
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
// Red once used a PARSE-rule-based file called %lexer.r, where the rules
// were formulated declaratively:
//
//     not-tag-1st: complement union ws-ASCII charset "=><[](){};^""
//
//     not-tag-char: complement charset ">"
//
//     tag-rule: [
//         #"<" s: not-tag-1st (type: tag!)
//         any [#"^"" thru #"^"" | #"'" thru #"'" | not-tag-char] e: #">"
//     ]
//
// But for "performance reasons", they moved toward something much more like
// the R3-Alpha scanner from which this scanner inherits.  :-(
//
// For expedience, Ren-C has been resigned to hacking on this scanner to add
// the many features that have been needed.  But the ultimate goal has always
// been to redo it in terms of a clear and declarative dialect that is used
// to generate efficient code.  It's a mess for now, but hopefully at some
// point the time will be made to create its replacement.
//

#include "sys-core.h"


const EscapeInfo g_escape_info[MAX_ESC + 1] = {  // must match EscapeCodeEnum
    {10, "line"},
    {9, "tab"},
    {12, "page"},
    {27, "escape"},
    {27, "esc"},
    {8, "back"},
    {127, "del"},
    {0, "null"}
};


// Prefer these to XXX_Executor_Flag(SCAN) in this file (much faster!)

#define Get_Scan_Executor_Flag(L,name) \
    (((L)->flags.bits & SCAN_EXECUTOR_FLAG_##name) != 0)

#define Not_Scan_Executor_Flag(L,name) \
    (((L)->flags.bits & SCAN_EXECUTOR_FLAG_##name) == 0)

#define Set_Scan_Executor_Flag(L,name) \
    ((L)->flags.bits |= SCAN_EXECUTOR_FLAG_##name)

#define Clear_Scan_Executor_Flag(L,name) \
    ((L)->flags.bits &= ~SCAN_EXECUTOR_FLAG_##name)


INLINE bool Is_Lex_Interstitial(Byte b)
  { return b == '/' or b == '.' or b == ':'; }

INLINE bool Is_Interstitial_Scan(Level* L)  // speedy check via flag
  { return Get_Scan_Executor_Flag(L, INTERSTITIAL_SCAN); }

INLINE bool Is_List_Scan(Level* L) {  // only used in errors, need not be fast
    Byte mode = LEVEL_STATE_BYTE(L);
    return mode == ']' or mode == ')' or mode == '}';
}

INLINE bool Is_Lex_Sub_Interstitial(Level *L, Byte sub) {
    Byte mode = LEVEL_STATE_BYTE(L);
    assert(Is_Lex_Interstitial(mode));
    assert(Is_Lex_Interstitial(sub));
    assert(mode != sub);
    if (mode == '/')
        return true;  // . and : are sub /
    if (mode == ':')
        return sub == '.';  // : is sub /, while / is above :
    return false;  // no sub-interstitial of tuple
}

INLINE bool Is_Lex_End_List(Byte b)
  { return b == ']' or b == ')' or b == '}'; }

INLINE bool Is_Dot_Or_Slash(Byte b)  // !!! Review lingering instances
  { return b == '/' or b == '.'; }

INLINE bool Interstitial_Match(Byte b, Byte mode) {
    assert(Is_Lex_Interstitial(mode));
    assert(Is_Lex_Interstitial(b));
    return b == mode;
}

INLINE bool Scan_Mode_Matches(Level* L, Byte mode) {
    assert(Is_Lex_Interstitial(mode) or Is_Lex_End_List(mode));
    return LEVEL_STATE_BYTE(L) == mode;
}

INLINE Sigil Sigil_From_Token(Token t) {
    assert(t != u_cast(int, SIGIL_0));
    assert(t <= u_cast(int, MAX_SIGIL));
    return u_cast(Sigil, t);
}


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
    /* 5F _   */    LEX_SPECIAL|LEX_SPECIAL_UNDERSCORE,

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
    /* 7E ~   */    LEX_DELIMIT|LEX_DELIMIT_TILDE,
    /* 7F DEL */    LEX_DEFAULT,

    // Odd Control Chars
    LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,    // 0x80
    LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
    LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
    LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
    LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
    LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
    LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,
    LEX_WORD,LEX_WORD,LEX_WORD,LEX_WORD,

    // Alternate Chars
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
// 1. While there is a line number and head of line in the TranscodeState, it
//    reflects the current position which isn't always the most useful.  e.g.
//    when you have a missing closing bracket, you want to know the bracket
//    that is not closed.
//
// 2. !!! The error should actually report both the file and line that is
//    running as well as the file and line being scanned.  Review.
//
// 3. !!! The file and line should likely be separated into an INTEGER! and
//    a FILE! so those processing the error don't have to parse it back out.
//
static void Update_Error_Near_For_Line(
    Error* error,
    TranscodeState* transcode,
    LineNumber line,  // may not come from transcode [1]
    const Byte* line_head  // [1]
){
    Set_Location_Of_Error(error, TOP_LEVEL);  // sets WHERE NEAR FILE LINE [2]

    const Byte* cp = line_head;  // skip indent (don't include in the NEAR
    while (Is_Lex_Space(*cp))
        ++cp;

    Size size = 0;
    const Byte* bp = cp;
    while (not ANY_CR_LF_END(*cp)) {  // find end of line to capture in message
        ++cp;
        ++size;
    }

    DECLARE_MOLDER (mo);  // put line count and line's text into string [3]
    Push_Mold(mo);
    Append_Ascii(mo->string, "(line ");
    Append_Int(mo->string, line);  // (maybe) different from line below
    Append_Ascii(mo->string, ") ");
    Append_UTF8_May_Fail(mo->string, cs_cast(bp), size, STRMODE_NO_CR);

    ERROR_VARS *vars = ERR_VARS(error);
    Init_Text(&vars->nearest, Pop_Molded_String(mo));

    if (transcode->file)
        Init_File(&vars->file, unwrap transcode->file);
    else
        Init_Nulled(&vars->file);

    Init_Integer(&vars->line, transcode->line);  // different from line above
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
    Init_Char_Unchecked(expected, wanted);

    Error* error = Error_Scan_Missing_Raw(expected);

    if (Is_Lex_End_List(wanted))
        Update_Error_Near_For_Line(
            error,
            S->transcode,
            S->start_line,
            S->start_line_head
        );
    else
        Update_Error_Near_For_Line(
            error,
            S->transcode,
            S->transcode->line,
            S->transcode->line_head
        );
    return error;
}


//
//  Try_Scan_UTF8_Char_Escapable: C
//
// Scan a char, handling ^A, ^/, ^(1234)
//
// Note that ^(null) from historical Rebol is no longer supported.
//
// Returns the numeric value for char, or nullptr for errors.
// 0 is a legal codepoint value which may be returned.
//
// Advances the cp to just past the last position.
//
// test: to-integer load to-blob mold to-char 1234
//
static Option(const Byte*) Try_Scan_UTF8_Char_Escapable(
    Codepoint *out,
    const Byte* bp
){
    Byte c = *bp;
    if (c == '\0')
        return nullptr;  // signal error if end of string

    if (c >= 0x80) {  // multibyte sequence
        Option(Error*) e = Trap_Back_Scan_Utf8_Char(out, &bp, nullptr);
        if (e) {
            UNUSED(e);  // !!! This should be Trap_Scan_Utf8_Char_Escapable()
            return nullptr;
        }
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

      case '(': {  // ^(tab) ^(1234)
        const Byte* cp = bp;  // restart location
        *out = 0;

        // Check for hex integers ^(1234)
        Byte nibble;
        while (Try_Get_Lex_Hexdigit(&nibble, *cp)) {
            *out = (*out << 4) + nibble;
            cp++;
        }
        if (*cp == ')') {
            cp++;
            return cp;
        }

        // Check for identifiers
        for (c = 0; c <= MAX_ESC; c++) {
            cp = maybe Try_Diff_Bytes_Uncased(
                bp, cb_cast(g_escape_info[c].name)
            );
            if (cp and *cp == ')') {
                bp = cp + 1;
                *out = g_escape_info[c].byte;
                return bp;
            }
        }
        return nullptr; }

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


#define CELL_FLAG_STACK_NOTE_BRACED CELL_FLAG_NOTE

// For compatibility to copy code to and from the bootstrap EXE, this code
// uses Mold_Buffer(mo) to abstract the difference from `mo->utf8flex`.
// (Calling the bootstrap exe's mold buffer a "string" would be potentially
// confusing since "String" doesn't use UTF-8 internally in that codebase.)
//
#define Mold_Buffer(mo) mo->string


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
//    as easy as possible to work with BLOB! using string-based routines
//    like FIND, etc., so use BLOB! if you need UTF-8 with '\0' in it.
//
static Option(Error*) Trap_Scan_String_Into_Mold_Core(
    Sink(const Byte*) out,
    Molder* mo,  // pushed by calling wrapper, dropped if error returned
    const Byte* bp,
    Count dashes,
    ScanState* S,  // used for errors
    StackIndex base  // accrue nest counts on stack
){
    const Byte* cp = bp;

    Init_Integer(PUSH(), dashes);  // so nest code is uniform

    if (*cp == '{')
        Set_Cell_Flag(TOP, STACK_NOTE_BRACED);
    else
        assert(*cp == '"');

    ++cp;

    while (true) {  // keep going until nesting levels all closed
        Codepoint c = *cp;

        switch (c) {
          case '\0': {
            if (Get_Cell_Flag(TOP, STACK_NOTE_BRACED))
                return Error_Missing(S, '}');
            return Error_Missing(S, '"'); }

          case '^':
            if (not (cp = maybe Try_Scan_UTF8_Char_Escapable(&c, cp)))
                return Error_User("Bad character literal in string");
            --cp;  // unlike Back_Scan_XXX, no compensation for ++cp later
            break;

          case '{': {  // brace with no leading dashes, nests if {a {b} c}
            if (
                Get_Cell_Flag(TOP, STACK_NOTE_BRACED)
                and 0 == VAL_INT32(TOP)  // > 0, e.g. -{a {b c}- won't nest
            ){
                Init_Integer(PUSH(), 0);
                Set_Cell_Flag(TOP, STACK_NOTE_BRACED);
            }
            break; }

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
                *cp == '{'
                and Get_Cell_Flag(TOP, STACK_NOTE_BRACED)
                and count >= VAL_INT32(TOP)
            ){
                Init_Integer(PUSH(), count);
                Set_Cell_Flag(TOP, STACK_NOTE_BRACED);
                Append_Codepoint(Mold_Buffer(mo), '{');
                ++cp;
            }
            /* else if (  // don't want "--" to start nested --" scan, rethink
                *cp == '"'
                and Not_Cell_Flag(TOP, STACK_NOTE_BRACED)
                and count >= VAL_INT32(TOP)
            ){
                Init_Integer(PUSH(), count);
                Append_Codepoint(Mold_Buffer(mo), '"');
            } */
            continue; }  // already appended all relevant codepoints

          case '"':
            if (Not_Cell_Flag(TOP, STACK_NOTE_BRACED))
                goto handle_closer;
            break;

          case '}':
            if (Get_Cell_Flag(TOP, STACK_NOTE_BRACED))
                goto handle_closer;
            break;

          handle_closer: {
            ++cp;
            Count count = 0;
            while (*cp == '-') {
                ++count;
                ++cp;
            }
            if (count > VAL_INT32(TOP))
                return Error_User("Nested }-- level closure too long");
            if (count == VAL_INT32(TOP)) {
                DROP();
                if (TOP_INDEX == base)
                    goto finished;  // end overall scan, don't add codepoints
            }

            if (Get_Cell_Flag(TOP, STACK_NOTE_BRACED))
                Append_Codepoint(Mold_Buffer(mo), '}');
            else
                Append_Codepoint(Mold_Buffer(mo), '"');

            for (; count != 0; --count)
                Append_Codepoint(Mold_Buffer(mo), '-');
            continue; }  // codepoints were appended already

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
            if (Not_Cell_Flag(TOP, STACK_NOTE_BRACED))
                return Error_User("Plain quoted strings not multi-line");
            ++S->transcode->line;
            break;

          default:
            if (c >= 0x80) {
                Option(Error*) e = Trap_Back_Scan_Utf8_Char(&c, &cp, nullptr);
                if (e)
                    return e;
            }
        }

        ++cp;

        if (c == '\0')  // e.g. ^(00) or ^@
            fail (Error_Illegal_Zero_Byte_Raw());  // illegal in strings [2]

        Append_Codepoint(Mold_Buffer(mo), c);
    }

  finished:

    *out = cp;
    return SUCCESS;
}


// Wrapper which handles dropping the stack and mold buffer on error case.
//
static Option(Error*) Trap_Scan_String_Push_Mold(
    Sink(const Byte*) out,
    Molder* mo,
    const Byte* bp,
    Count dashes,
    ScanState* S  // used for errors
){
    StackIndex base = TOP_INDEX;
    Push_Mold(mo);
    Option(Error*) e = Trap_Scan_String_Into_Mold_Core(
        out, mo, bp, dashes, S, base
    );
    if (e) {
        Drop_Data_Stack_To(base);
        Drop_Mold(mo);
    }
    return e;
}


// This does a scan of a UTF-8 item like a FILE! or an ISSUE!, when it's not
// enclosed in quotes.  This means it's terminated by delimiters--such as a
// space or a closing bracket, parentheses, or brace.  However, we want things
// like %(get $dir)/foo.bar to be legal, and since we're aiming to put code
// in these places which might have things like strings saying ")" we need to
// use the scanner's logic for GROUP! or BLOCK! or FENCE!.
//
// Puts result into the temporary mold buffer as UTF-8.
//
// 1. This code once forced %\foo\bar to become %/foo/bar.  That's dodgy.
//    (URL! has already come under scrutiny for these kinds of automatic
//    translations that affect round-trip copy and paste, and it seems
//    applicable to FILE! too.)
//
Option(Error*) Trap_Scan_Utf8_Item_Into_Mold(
    Sink(const Byte*) end_out,
    Molder* mo,
    const Byte* begin,
    Token token,
    ScanState* S
){
    Option(const Byte*) invalids;
    if (token == TOKEN_FILE)  // percent-encoded historically :-/
        invalids = cb_cast(":;\"");
    else {
        assert(token == TOKEN_ISSUE);
        invalids = nullptr;
    }

    String* buf = Mold_Buffer(mo);

    const Byte* cp = begin;

    while (
        *cp != '\0'
        and (not Is_Codepoint_Whitespace(*cp))
        and *cp != ']' and *cp != ')' and *cp != '}' and *cp != ','
    ){
        Codepoint c = *cp;  // may be first byte of UTF-8 encoded char

      call_scanner_if_list: { ////////////////////////////////////////////////

        if (c == '(' or c == '[' or c == '{') {
            Byte terminal = End_Delimit_For_Char(cast(Byte, c));

            TranscodeState transcode;
            Init_Transcode(  // don't make scanner re-scan the '(', use cp + 1
                &transcode, S->transcode->file, S->transcode->line, cp + 1
            );

            Flags flags = (
                 FLAG_STATE_BYTE(Scanner_State_For_Terminal(terminal))
              /* | LEVEL_FLAG_RAISED_RESULT_OK */  // definitional errors?
            );

            Level* scan = Make_Scan_Level(&transcode, TG_End_Feed, flags);

            DECLARE_ATOM (discard);
            Push_Level_Erase_Out_If_State_0(discard, scan);
            bool threw = Trampoline_With_Top_As_Root_Throws();
            Drop_Data_Stack_To(scan->baseline.stack_base);  // !!! new mode?
            Drop_Level(scan);

            if (threw) {
                DECLARE_VALUE (label);
                Copy_Cell(label, VAL_THROWN_LABEL(TOP_LEVEL));
                assert(Is_Error(label));

                DECLARE_ATOM (arg);
                CATCH_THROWN(arg, TOP_LEVEL);

                return Cell_Error(label);
            }

            Size size = transcode.at - cp;
            Size original_used = Binary_Len(buf);
            Expand_Flex_Tail(buf, size);  // updates used size
            Byte* dest = Binary_At(buf, original_used);
            Length len = 0;
            for (; cp != transcode.at; ++cp, ++dest) {
                if (not Is_Continuation_Byte(*cp))
                    ++len;
                *dest = *cp;
            }
            Term_String_Len_Size(
                buf, String_Len(buf) + len, original_used + size
            );
            continue;
        }

    } handle_hex_encoded_chars: { ////////////////////////////////////////////

        // 1. !!! Filename hex-encoding (if it's a good idea at all) appears
        //    to predate UTF-8, so it only decoded one byte.  Most likely this
        //    should just be deleted.

        if (token == TOKEN_FILE and c == '%') {
            Byte decoded;
            if (not (cp = maybe Try_Scan_Hex2(&decoded, cp + 1)))
                return Error_User("Bad Hex Encoded Character");
            c = decoded;
            if (c >= 0x80)
                return Error_User(  // [1]
                    "Hex encoding for UTF-8 in Filenames not supported yet"
                );
            goto check_for_invalid_ascii;
        }

    } handle_caret_encoded_chars: { //////////////////////////////////////////

        // !!! Rebol encoding is up in the air as to if it will be kept.

        if (c == '^') {  // Accept ^X encoded char:
            if (not (cp = maybe Try_Scan_UTF8_Char_Escapable(&c, cp)))
                return SUCCESS;
            goto check_for_invalid_unicode;
        }

    } handle_multibyte_utf8_chars: { /////////////////////////////////////////

        if (c >= 0x80) {
            Option(Error*) e = Trap_Back_Scan_Utf8_Char(&c, &cp, nullptr);
            if (e)
                return e;
            ++cp;  // UTF-8 back scanning doesn't do the increment
            goto check_for_invalid_unicode;
        }

        ++cp;
        goto check_for_invalid_ascii;

    } check_for_invalid_unicode: { //////////////////////////////////////////

        // None of these declared invalid yet (but probably should)
        // would fall through to the check_for_invalid_ascii

    } check_for_invalid_ascii: { /////////////////////////////////////////////

        if (c >= 128 or not invalids) {
            // not valid ASCII, so don't check it
        }
        else if (strchr(cs_cast(unwrap invalids), c)) {
            //
            // Is char as literal valid? (e.g. () [] etc.)
            // Only searches ASCII characters.
            //
            return Error_User("Invalid character in filename");
        }
        else if (c < ' ') {
            return Error_User("Control characters not allowed in filenames");
        }
        else if (c == '\\') {
            return Error_User("Backslash not allowed in filenames");  // [1]
        }

    } append_codepoint_and_continue: { ///////////////////////////////////////

        Append_Codepoint(buf, c);

    }}

    *end_out = cp;

    return SUCCESS;
}


// Wrapper which handles dropping the stack and mold buffer on error case.
//
Option(Error*) Trap_Scan_Utf8_Item_Push_Mold(
    Sink(const Byte*) out,
    Molder* mo,
    const Byte* bp,
    Token token,
    ScanState* S  // used for errors
){
    Push_Mold(mo);
    Option(Error*) e = Trap_Scan_Utf8_Item_Into_Mold(
        out, mo, bp, token, S
    );
    if (e) {
        Drop_Mold(mo);
        return e;
    }
    return SUCCESS;
}


//
//  Seek_To_End_Of_Tag: C
//
// Skip the entire contents of a tag, including quoted strings and newlines.
// The argument points to the opening '<'.  nullptr is returned on errors.
//
static const Byte* Seek_To_End_Of_Tag(const Byte* cp)
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
    assert(S->end >= S->begin);  // can get out of sync [1]

    DECLARE_ELEMENT (token_name);
    Init_Text(token_name, Make_String_UTF8(g_token_names[token]));

    DECLARE_ELEMENT (token_text);
    Init_Text(
        token_text,
        Make_Sized_String_UTF8(cs_cast(S->begin), S->end - S->begin)
    );

    return Error_Scan_Invalid_Raw(token_name, token_text);
}


//
//  Error_Extra: C
//
// For instance, `load "abc ]"`
//
static Error* Error_Extra(Byte seen) {
    DECLARE_ELEMENT (unexpected);
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
static Error* Error_Mismatch(Byte wanted, Byte seen) {
    DECLARE_ELEMENT (w);
    Init_Char_Unchecked(w, wanted);
    DECLARE_ELEMENT (s);
    Init_Char_Unchecked(s, seen);
    return Error_Scan_Mismatch_Raw(w, s);
}


//
//  Prescan_Fingerprint: C
//
// This function updates `S->begin` to skip past leading whitespace.  If the
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
// Get_Lex_Class(S->begin[0]).  Fingerprinting just helps accelerate further
// categorization.
//
static LexFlags Prescan_Fingerprint(ScanState* S)
{
    assert(Is_Pointer_Corrupt_Debug(S->end));  // prescan only uses ->begin

    const Byte* cp = S->transcode->at;
    LexFlags flags = 0;  // flags for all LEX_SPECIALs seen after S->begin[0]

    while (Is_Lex_Space(*cp))  // skip whitespace (if any)
        ++cp;
    S->begin = cp;  // don't count leading whitespace as part of token

    while (true) {
        switch (Get_Lex_Class(*cp)) {
          case LEX_CLASS_DELIMIT:
            if (cp == S->begin) {
                //
                // Include the delimiter if it is the only character we
                // are returning in the range (leave it out otherwise)
                //
                S->end = cp + 1;

                // Note: We'd liked to have excluded LEX_DELIMIT_END, but
                // would require a Get_Lex_Delimit() call to know to do so.
                // Locate_Token_May_Push_Mold() does a `switch` on that,
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
            ++cp;
            break;

          case LEX_CLASS_WORD:
            //
            // If something is in LEX_CLASS_SPECIAL it gets set in the flags
            // that are returned.  But if any member of LEX_CLASS_WORD is
            // found, then a flag will be set indicating that also.
            //
            Set_Lex_Flag(flags, LEX_SPECIAL_WORD);
            while (Is_Lex_Word_Or_Number(*cp))
                ++cp;
            break;

          case LEX_CLASS_NUMBER:
            while (Is_Lex_Number(*cp))
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


// Make it a little cleaner to return tokens from Trap_Locate_Token, returning
// nullptr for the error, captures the pattern:
//
//    *token = TOKEN_XXX;
//    return nullptr;
//
// as just `return LOCATED(TOKEN_XXX);`
//
#define LOCATED(tok) (*token_out = tok, SUCCESS)


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
// conclusion at a delimiter.  The calculated token will be returned.
//
// The TOKEN_XXX type returned will correspond directly to a Rebol datatype
// if it isn't an ANY-LIST? (e.g. TOKEN_INTEGER for INTEGER! or TOKEN_STRING
// for STRING!).  When a block or group delimiter is found it will indicate
// that, e.g. TOKEN_BLOCK_BEGIN will be returned to indicate the scanner
// should recurse... or TOKEN_GROUP_END which will signal the end of a level
// of recursion.
//
// TOKEN_END is returned if end of input is reached.
//
// Newlines that should be internal to a non-ANY-LIST? type are included in
// the scanned range between the `begin` and `end`.  But newlines that are
// found outside of a string are returned as TOKEN_NEWLINE.  (These are used
// to set the CELL_FLAG_NEWLINE_BEFORE bits on the next value.)
//
// Determining the end point of token types that need escaping requires
// processing (for instance `{a^}b}` can't see the first close brace as ending
// the string).  To avoid double processing, the routine decodes the string's
// content into the mold buffer for any quoted form used by the caller.  It's
// overwritten in successive calls, and is only done for quoted forms (e.g.
// %"foo" will have data in the mold buffer but %foo will not.)
//
// !!! This is a somewhat weird separation of responsibilities, that seems to
// arise from a desire to make "Scan_XXX" functions independent of the
// "Trap_Locate_Token_May_Push_Mold" function.  But if work on locating the
// value means you have to basically do what you'd do to read it into a cell
// anyway, why split it?  This is especially true now that the variadic
// splicing pushes values directly from this routine.
//
// Error handling is limited for most types, as an additional phase is needed
// to load their data into a REBOL value.  Yet if a "cheap" error is
// incidentally found during this routine without extra cost to compute, it
// will return that error.
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
//     {line1\nline2}  => TOKEN_STRING (content in mold buffer)
//     B             E
//
//     \n{line2} => TOKEN_NEWLINE (newline is external)
//     BB
//       E
//
//     %"a ^"b^" c" d => TOKEN_FILE (content in mold buffer)
//     B           E
//
//     %a-b.c d => TOKEN_FILE (content *not* in mold buffer)
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
    Sink(Token) token_out,
    Molder* mo,
    Level* L
){
    ScanState* S = &L->u.scan;
    TranscodeState* transcode = S->transcode;

    Corrupt_Pointer_If_Debug(S->begin);  // S->begin skips ->at's whitespace
    Corrupt_Pointer_If_Debug(S->end);  // this routine should set S->end

  acquisition_loop: //////////////////////////////////////////////////////////

    // This supports scanning of variadic material, e.g. C code like:
    //
    //     Value* some_value = rebInteger(3);
    //     rebElide("print [{The value is}", some_value, "]");
    //
    // We scan one string component at a time, pushing the appropriate items.
    // Each time a UTF-8 source fragment being scanned is exhausted, ->at
    // will be set to nullptr and this loop is run to see if there's more
    // input to be processed--either values to splice, or other fragments
    // of UTF-8 source.
    //
    // See the "Feed" abstraction for the mechanics by which these text and
    // spliced components are fed to the scanner (and then optionally to the
    // evaluator), potentially bypassing the need to create an intermediary
    // BLOCK! structure to hold the code.
    //
    while (not transcode->at) {
        if (L->feed->p == nullptr) {  // API null, can't be in feed, use BLANK
            Init_Quasi_Null(PUSH());
            Set_Cell_Flag(TOP, FEED_NOTE_META);
            if (Get_Scan_Executor_Flag(L, NEWLINE_PENDING)) {
                Clear_Scan_Executor_Flag(L, NEWLINE_PENDING);
                Set_Cell_Flag(TOP, NEWLINE_BEFORE);
            }
        }
        else switch (Detect_Rebol_Pointer(L->feed->p)) {
          case DETECTED_AS_END:
            L->feed->p = &PG_Feed_At_End;
            return LOCATED(TOKEN_END);

          case DETECTED_AS_CELL: {
            Copy_Reified_Variadic_Feed_Cell(
                PUSH(),
                c_cast(Value*, L->feed->p)
            );
            if (Get_Scan_Executor_Flag(L, NEWLINE_PENDING)) {
                Clear_Scan_Executor_Flag(L, NEWLINE_PENDING);
                Set_Cell_Flag(TOP, NEWLINE_BEFORE);
            }
            break; }

          case DETECTED_AS_STUB: {  // e.g. rebQ, rebU, or a rebR() handle
            Option(const Element*) e = Try_Reify_Variadic_Feed_At(L->feed);
            if (not e)
                goto get_next_variadic_pointer;

            Copy_Cell(PUSH(), unwrap e);
            if (Get_Scan_Executor_Flag(L, NEWLINE_PENDING)) {
                Clear_Scan_Executor_Flag(L, NEWLINE_PENDING);
                Set_Cell_Flag(TOP, NEWLINE_BEFORE);
            }
            break; }

          case DETECTED_AS_UTF8: {  // String segment, scan it ordinarily.
            transcode->at = c_cast(Byte*, L->feed->p);  // breaks the loop...

            // If we're using a va_list, we start the scan with no C string
            // pointer to serve as the beginning of line for an error message.
            // wing it by just setting the line pointer to whatever the start
            // of the first UTF-8 string fragment we see.
            //
            // !!! A more sophisticated debug mode might "reify" the va_list
            // as a BLOCK! before scanning, which might be able to give more
            // context for the error-causing input.
            //
            if (not transcode->line_head) {
                assert(FEED_VAPTR(L->feed) or FEED_PACKED(L->feed));
                assert(not S->start_line_head);
                S->start_line_head = transcode->line_head = S->begin;
            }
            break; }

          default:
            assert(false);
        }

      get_next_variadic_pointer:

        if (FEED_VAPTR(L->feed))
            L->feed->p = va_arg(*(unwrap FEED_VAPTR(L->feed)), const void*);
        else
            L->feed->p = *FEED_PACKED(L->feed)++;
    }

    LexFlags flags = Prescan_Fingerprint(S);  // sets ->begin, skips whitespace

    const Byte* cp = S->begin;

    if (*cp == '-') {  // first priority: -{...}- --{...}--
        Count dashes = 1;
        const Byte* dp = cp;
        for (++dp; *dp == '-'; ++dp)
            ++dashes;
        if (*dp == '{' or *dp == '"') {
            Option(Error*) error = Trap_Scan_String_Push_Mold(
                &cp, mo, dp, dashes, S
            );
            if (error)
                return error;
            goto check_str;
        }
    }
    else if (*cp == '^') {
        S->end = cp + 1;
        return LOCATED(TOKEN_CARET);
    }
    else if (*cp == '@') {
        S->end = cp + 1;
        return LOCATED(TOKEN_AT);
    }
    else if (*cp == '&') {
        S->end = cp + 1;
        return LOCATED(TOKEN_AMPERSAND);
    }
    else if (
        *cp == '$'
        and Get_Lex_Class(cp[1]) != LEX_CLASS_NUMBER
        and not (cp[1] == '-' and Get_Lex_Class(cp[2]) == LEX_CLASS_NUMBER)
    ){
        S->end = cp + 1;
        return LOCATED(TOKEN_DOLLAR);
    }

    Token token;  // only set if falling through to `scan_word`

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
            if (temp != S->end)
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

            return LOCATED(TOKEN_WORD);
        }
    }

    switch (Get_Lex_Class(*cp)) {
      case LEX_CLASS_DELIMIT:
        switch (Get_Lex_Delimit(*cp)) {
          case LEX_DELIMIT_SPACE:
            panic ("Prescan_Token did not skip whitespace");

          case LEX_DELIMIT_RETURN:
          delimit_return: {
            //
            // !!! Ren-C is attempting to rationalize and standardize Rebol
            // on line feeds only.  If for some reason we wanted a tolerant
            // mode, that tolerance would go here.  Note that this code does
            // not cover the case of CR that are embedded in multi-line
            // string literals.
            //
            enum Reb_Strmode strmode = STRMODE_NO_CR;  // transcode->strmode ?
            if (strmode == STRMODE_CRLF_TO_LF) {
                if (cp[1] == LF) {
                    ++cp;
                    goto delimit_line_feed;
                }
            }
            else
                assert(strmode == STRMODE_NO_CR);

            return Error_Illegal_Cr(cp, S->begin); }

          case LEX_DELIMIT_LINEFEED:
          delimit_line_feed:
            ++transcode->line;
            S->end = cp + 1;
            return LOCATED(TOKEN_NEWLINE);

          case LEX_DELIMIT_LEFT_BRACKET:  // [BLOCK] begin
            return LOCATED(TOKEN_BLOCK_BEGIN);

          case LEX_DELIMIT_RIGHT_BRACKET:  // [BLOCK] end
            return LOCATED(TOKEN_BLOCK_END);

          case LEX_DELIMIT_LEFT_PAREN:  // (GROUP) begin
            return LOCATED(TOKEN_GROUP_BEGIN);

          case LEX_DELIMIT_RIGHT_PAREN:  // (GROUP) end
            return LOCATED(TOKEN_GROUP_END);

          case LEX_DELIMIT_LEFT_BRACE:  // {FENCE} begin
            return LOCATED(TOKEN_FENCE_BEGIN);

          case LEX_DELIMIT_RIGHT_BRACE:  // {FENCE} end
            return LOCATED(TOKEN_FENCE_END);

          case LEX_DELIMIT_DOUBLE_QUOTE: {  // "QUOTES"
            Option(Error*) error = Trap_Scan_String_Push_Mold(
                &cp, mo, cp, 0, S
            );
            if (error)
                return error;
            goto check_str; }

          check_str:
            if (cp) {
                S->end = cp;
                return LOCATED(TOKEN_STRING);
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

            panic ("Invalid string start delimiter");

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
                    or Is_Lex_End_List(cp[1])
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
            return Error_Unknown_Error_Raw(); }

          case LEX_DELIMIT_END:
            //
            // We've reached the end of this string token's content.  By
            // putting nullptr in S->begin, that cues the acquisition loop
            // to check if there's a variadic pointer in effect to see if
            // there's more content yet to come.
            //
            transcode->at = nullptr;
            Corrupt_Pointer_If_Debug(S->begin);
            Corrupt_Pointer_If_Debug(S->end);
            goto acquisition_loop;

          case LEX_DELIMIT_COMMA:
            ++cp;
            S->end = cp;
            if (*cp == ',' or not Is_Lex_Delimit(*cp)) {
                ++S->end;  // don't allow `,,` or `a,b` etc.
                return Error_Syntax(S, TOKEN_COMMA);
            }
            return LOCATED(TOKEN_COMMA);

          case LEX_DELIMIT_TILDE:
            assert(*cp == '~');
            S->end = cp + 1;
            return LOCATED(TOKEN_TILDE);

          default:
            panic ("Invalid LEX_DELIMIT class");
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

        if (
            Has_Lex_Flag(flags, LEX_SPECIAL_AT)  // @ anywhere but at the head
            and *cp != '<'  // want <foo="@"> to be a TAG!, not an EMAIL!
            and *cp != '\''  // want '@foo to be a ... ?
            and *cp != '#'  // want #@ to be an ISSUE! (charlike)
        ){
            if (*cp == '@')  // consider `@a@b`, `@@`, etc. ambiguous
                return Error_Syntax(S, TOKEN_EMAIL);

            token = TOKEN_EMAIL;
            goto prescan_subsume_all_dots;
        }

      next_lex_special:

        switch (Get_Lex_Special(*cp)) {
          case LEX_SPECIAL_AT:  // the case where @ is actually at the head
            assert(false);  // already taken care of
            panic ("@ dead end");

          case LEX_SPECIAL_PERCENT:  // %filename
            ++cp;
            if (*cp == '%') {  // %% is WORD! exception
                if (not Is_Lex_Delimit(cp[1]) and cp[1] != ':') {
                    S->end = cp + 2;
                    return Error_Syntax(S, TOKEN_FILE);
                }
                S->end = cp + 1;
                return LOCATED(TOKEN_WORD);
            }

            token = TOKEN_FILE;

          issue_or_file_token: {  // issue jumps here, should set `token`
            assert(token == TOKEN_FILE or token == TOKEN_ISSUE);

            if (*cp == ';') {
                //
                // !!! This used to be illegal in `#;` but should it be?  Is
                // there more value in allowing `#a;b` or similar, as with
                // URL! having semicolons intenrally?
            }
            if (*cp == '"') {
                /*Option(const Byte*) invalids = cb_cast(":;\""); */

                Option(Error*) e = Trap_Scan_String_Push_Mold(
                    &cp, mo, cp, 0, S
                );
                if (e)
                    return e;
                S->end = cp;
                return LOCATED(token);
            }
            Option(Error*) e = Trap_Scan_Utf8_Item_Push_Mold(
                &cp, mo, cp, token, S
            );
            if (e)
                return e;
            S->end = cp;
            return LOCATED(token); }

          case LEX_SPECIAL_APOSTROPHE:
            while (*cp == '\'')  // get sequential apostrophes as one token
                ++cp;
            S->end = cp;
            return LOCATED(TOKEN_APOSTROPHE);

          case LEX_SPECIAL_GREATER:  // arrow words like `>` handled above
            return Error_Syntax(S, TOKEN_TAG);

          case LEX_SPECIAL_LESSER:
            cp = Seek_To_End_Of_Tag(cp);
            if (
                not cp  // couldn't find ending `>`
                or not (
                    Is_Lex_Delimit(*cp)
                    or Is_Lex_Whitespace(*cp)  // `<abc>def` not legal
                )
            ){
                return Error_Syntax(S, TOKEN_TAG);
            }
            S->end = cp;
            return LOCATED(TOKEN_TAG);

          case LEX_SPECIAL_PLUS:  // +123 +123.45
          case LEX_SPECIAL_MINUS:  // -123 -123.45
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
                if ((Get_Lex_Special(*cp)) == LEX_SPECIAL_WORD)
                    goto next_lex_special;
                if (*cp == '+' or *cp == '-')
                    goto prescan_word;
                return Error_Syntax(S, TOKEN_WORD);
            }
            goto prescan_word;

          case LEX_SPECIAL_BAR:
            goto prescan_word;

          case LEX_SPECIAL_UNDERSCORE:
            //
            // `_` standalone should become a BLANK!, so if followed by a
            // delimiter or space.  However `_a_` and `a_b` are left as
            // legal words (at least for the time being).
            //
            if (Is_Lex_Delimit(cp[1]) or Is_Lex_Whitespace(cp[1]))
                return LOCATED(TOKEN_BLANK);
            goto prescan_word;

          case LEX_SPECIAL_POUND:
          pound:
            ++cp;
            if (*cp == '[') {
                S->end = ++cp;
                return LOCATED(TOKEN_CONSTRUCT);
            }
            if (*cp == '"') {  // CHAR #"C"
                S->end = S->begin;
                S->begin = cp;
                Option(Error*) e = Trap_Scan_String_Push_Mold(
                    &cp, mo, cp, 0, S
                );
                if (e)
                    return e;
                S->begin = S->end;  // restore start
                S->end = cp;
                return LOCATED(TOKEN_CHAR);
            }
            if (*cp == '{') {  // BINARY #{12343132023902902302938290382}
                S->end = S->begin;  // save start
                S->begin = cp;
                Option(Error*) e = Trap_Scan_String_Push_Mold(
                    &cp, mo, cp, 0, S
                );
                Drop_Mold(mo);  // not used...?
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

                // !!! This was Error_Syntax(S, TOKEN_BINARY), but if we use
                // the same error as for an unclosed string the console uses
                // that to realize the binary may be incomplete.  It may also
                // have bad characters in it, but that would be detected by
                // the caller, so we mention the missing `}` first.)
                //
                return Error_Missing(S, '}');
            }
            token = TOKEN_ISSUE;
            goto issue_or_file_token;  // different policies on / : .

          case LEX_SPECIAL_DOLLAR:
            if (
                cp[1] == '$' or cp[1] == ':' or Is_Lex_Delimit(cp[1])
            ){
                while (*cp == '$')
                    ++cp;
                S->end = cp;
                return LOCATED(TOKEN_WORD);
            }
            if (Has_Lex_Flag(flags, LEX_SPECIAL_AT)) {
                token = TOKEN_EMAIL;
                goto prescan_subsume_all_dots;
            }
            token = TOKEN_MONEY;
            goto prescan_subsume_up_to_one_dot;

          case LEX_SPECIAL_UTF8_ERROR:
            return Error_Syntax(S, TOKEN_WORD);

          default:
            return Error_Syntax(S, TOKEN_WORD);
        }

      case LEX_CLASS_WORD:
        if (
            Only_Lex_Flag(flags, LEX_SPECIAL_WORD)
            and *S->end != ':'  // need additional scan for URL if word://
        ){
            return LOCATED(TOKEN_WORD);
        }
        goto prescan_word;

      case LEX_CLASS_NUMBER:  // Note: "order of tests is important"
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
            if (cp == S->begin) {  // no +2 +16 +64 allowed
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
            return Error_Syntax(S, TOKEN_INTEGER);
        }

        if (Has_Lex_Flag(flags, LEX_SPECIAL_POUND)) { // -#123 2#1010
            if (
                Has_Lex_Flags(
                    flags,
                    ~(
                        LEX_FLAG(LEX_SPECIAL_POUND)
                        /* | LEX_FLAG(LEX_SPECIAL_PERIOD) */  // !!! What?
                        | LEX_FLAG(LEX_SPECIAL_APOSTROPHE)
                    )
                )
            ){
                return Error_Syntax(S, TOKEN_INTEGER);
            }
            return LOCATED(TOKEN_INTEGER);
        }

        // Note: R3-Alpha supported dates like `1/2/1998`, despite the main
        // date rendering showing as 2-Jan-1998.  This format was removed
        // because it is more useful to have `1/2` and other numeric-styled
        // PATH!s for use in dialecting.
        //
        for (; cp != S->end; cp++) {
            // what do we hit first? 1-AUG-97 or 123E-4
            if (*cp == '-')
                return LOCATED(TOKEN_DATE);  // 1-2-97 1-jan-97
            if (*cp == 'x' or *cp == 'X')
                return LOCATED(TOKEN_PAIR);  // 320x200
            if (*cp == 'E' or *cp == 'e') {
                if (Skip_To_Byte(cp, S->end, 'x'))
                    return LOCATED(TOKEN_PAIR);
                return LOCATED(TOKEN_DECIMAL);  // 123E4
            }
            if (*cp == '%')
                return LOCATED(TOKEN_PERCENT);

            if (Is_Dot_Or_Slash(*cp)) {  // will be part of a TUPLE! or PATH!
                S->end = cp;
                return LOCATED(TOKEN_INTEGER);
            }
        }
        if (Has_Lex_Flag(flags, LEX_SPECIAL_APOSTROPHE))  // 1'200
            return LOCATED(TOKEN_INTEGER);
        return Error_Syntax(S, TOKEN_INTEGER);

      default:
        break;  // panic after switch, so no cases fall through accidentally
    }

    panic ("Invalid LEX class");

  prescan_word: { /////////////////////////////////////////////////////////////

   if (*S->end == ':') {  // word:  url:words
        cp = S->end + 1;
        if (*cp == ':') {
            // saw :: which we treat as URL, e.g. log::error
        }
        else if (*cp == '/') {
            ++cp;  // saw `:/`
            if (*cp != '/')
                return LOCATED(TOKEN_WORD);
            // saw `://`
        }
        else
            return LOCATED(TOKEN_WORD);

        StackIndex base = TOP_INDEX;
        while (true) {  // saw `::` or `://`, okay treat as URL, look for its end
            ++cp;

            while (Is_Lex_Not_Delimit(*cp))
                ++cp;  // not delimiter, e.g. `http://example.com]` stops it

            switch (*cp) {
              case '[':
                Init_Char_Unchecked(PUSH(), ']');
                continue;

              case '(':
                Init_Char_Unchecked(PUSH(), ')');
                continue;

              case '{':
                Init_Char_Unchecked(PUSH(), '}');
                continue;

              case ']':
              case ')':
              case '}':
                if (base == TOP_INDEX) {  // closing the code
                    S->end = cp;
                    return LOCATED(TOKEN_URL);
                }

                if (*cp != Cell_Codepoint(TOP)) {
                    Byte want = cast(Byte, Cell_Codepoint(TOP));
                    Drop_Data_Stack_To(base);
                    return Error_Mismatch(want, *cp);
                }
                DROP();
                continue;  // loop will increment

              default:
                break;
            }
            if (TOP_INDEX != base)
                continue;  // allow http://(what + ever, you want).com
            if (Is_Lex_Interstitial(*cp) or not Is_Lex_Delimit_Hard(*cp))
                continue;  // slash, dots, and colons legal... and tilde
            break;  // other delimiters are not
        }

        if (base != TOP_INDEX) {
            Byte want = cast(Byte, Cell_Codepoint(TOP));
            Drop_Data_Stack_To(base);
            return Error_Mismatch(want, *cp);
        }

        S->end = cp;
        return LOCATED(TOKEN_URL);
    }
    if (Has_Lex_Flag(flags, LEX_SPECIAL_AT)) {
        token = TOKEN_EMAIL;
        goto prescan_subsume_all_dots;
    }
    if (Has_Lex_Flag(flags, LEX_SPECIAL_DOLLAR)) {  // !!! XYZ$10.20 ??
        token = TOKEN_MONEY;
        goto prescan_subsume_up_to_one_dot;
    }
    if (Has_Lex_Flags(flags, LEX_FLAGS_NONWORD_SPECIALS))
        return Error_Syntax(S, TOKEN_WORD);  // has non-word chars (eg % \ )
    if (
        Has_Lex_Flag(flags, LEX_SPECIAL_LESSER)
        or Has_Lex_Flag(flags, LEX_SPECIAL_GREATER)
    ){
        return Error_Syntax(S, TOKEN_WORD);  // arrow words handled way above
    }

    return LOCATED(TOKEN_WORD);

} prescan_subsume_up_to_one_dot: { ////////////////////////////////////////////

    assert(token == TOKEN_MONEY or token == TOKEN_TIME);

    // By default, `.` is a delimiter class which stops token scaning.  So if
    // scanning $10.20 or $3.04, there is common code to look past the
    // delimiter hit.  The same applies to times.  (DECIMAL! has its own code)
    //
    // !!! This is all hacked together at this point, CHAIN! threw in more
    // curveballs as a delimiter class.  It is now believed that backtick
    // literals are the right answer, e.g. `10:20` can be a time while 10:20
    // can be a CHAIN!.

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

    assert(token == TOKEN_EMAIL);

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
//  Init_Transcode: C
//
// Initialize a state structure for capturing the global state of a transcode.
//
void Init_Transcode(
    TranscodeState* transcode,
    Option(const String*) file,
    LineNumber line,
    Option(const Byte*) bp
){
    transcode->file = file;

    transcode->at = maybe bp;
    transcode->line_head = transcode->at;
    transcode->line = line;
}


//
//  Make_Scan_Level: C
//
// Initialize the per-level scanner state structure.  Note that whether this
// will be a variadic transcode or not is based on the Level's "Feed".
//
Level* Make_Scan_Level(
    TranscodeState* transcode,
    Feed* feed,
    Flags flags
){
    Level* L = Make_Level(&Scanner_Executor, feed, flags);

    Byte mode = LEVEL_STATE_BYTE(L);
    assert(mode != 0);  // must use non-zero state byte
    if (mode == '/' or mode == ':' or mode == '.')
        assert(flags & SCAN_EXECUTOR_FLAG_INTERSTITIAL_SCAN);
    UNUSED(mode);

    ScanState* S = &L->u.scan;
    S->transcode = transcode;

    S->start_line_head = transcode->line_head;
    S->start_line = transcode->line;
    S->num_quotes_pending = 0;
    S->sigil_pending = SIGIL_0;
    S->quasi_pending = false;

    Corrupt_Pointer_If_Debug(S->begin);
    Corrupt_Pointer_If_Debug(S->end);

    return L;
}


// This function is called when we reach the end of a scan or end list
// delimiter but have seen only sigils and quotes.  So "[~]" or "[$]" would be
// called here when it sees the "]".
//
// 1. At one time, ' was a SIGIL!, and the answer to (sigil of first ['foo]).
//    It has been reclaimed as an illegal state, so it might be used for
//    other out of band purposes in the scanner, such as line continuation.
//
static Option(Error*) Trap_Flush_Pending_Sigils(ScanState* S) {
    if (S->sigil_pending) {  // e.g. "$]" or "''$]"
        assert(not S->quasi_pending);
        Init_Sigil(PUSH(), unwrap S->sigil_pending);
        S->sigil_pending = SIGIL_0;
    }
    else if (S->quasi_pending) {  // "~]" or "''~]"
        Init_Trash(PUSH());
        S->quasi_pending = false;
    }
    else if (S->num_quotes_pending) {  // "']" or "''']" are illegal [1]
        return Error_Syntax(S, TOKEN_APOSTROPHE);
    }

    if (S->num_quotes_pending != 0) {
        Quotify_Depth(TOP_ELEMENT, S->num_quotes_pending);
        S->num_quotes_pending = 0;
    }
    return SUCCESS;
}

static Option(Error*) Trap_Apply_Pending_Decorations(
    ScanState* S,
    OnStack(Element*) top
){
    if (S->sigil_pending) {
        Heart heart = Heart_Of_Builtin_Fundamental(top);
        if (not Any_Plain_Type(heart))
            return Error_Syntax(S, TOKEN_BLANK);  // !!! token?

        HEART_BYTE(top) = Sigilize_Any_Plain_Heart(
            unwrap S->sigil_pending,
            heart
        );
        S->sigil_pending = SIGIL_0;
    }
    if (S->num_quotes_pending != 0) {
        Quotify_Depth(top, S->num_quotes_pending);
        S->num_quotes_pending = 0;
    }
    return SUCCESS;
}


//=//// SCANNER-SPECIFIC RAISE HELPER /////////////////////////////////////=//
//
// Override the RAISE macro for returning definitional errors.  It adds a
// capture of the `transcode` state local variable in Scanner_Executor(),
// so it can augment any error you give with the scanner's location.
//
// 1. Some errors have more useful information to put in the "near", so this
//    only adds it to errors that don't have that.  An example of a more
//    specific error is that when you have an unclosed brace, it reports the
//    opening location--not the end of the file (which is where the global
//    transcode state would be when it made the discovery it was unmatched).
//

INLINE Bounce Scanner_Raise_Helper(
    TranscodeState* transcode,
    Level* level_,
    Error* error
){
    ERROR_VARS *vars = ERR_VARS(error);
    if (Is_Nulled(&vars->nearest))  // only update if it doesn't have it [1]
        Update_Error_Near_For_Line(
            error, transcode, transcode->line, transcode->line_head
        );
    return Native_Raised_Result(level_, error);
}

#undef RAISE
#define RAISE(p) \
    Scanner_Raise_Helper(transcode, level_, Derive_Error_From_Pointer(p))


INLINE Bounce Scanner_Fail_Helper(
    TranscodeState* transcode,
    Level* level_,
    Error* error
){
    ERROR_VARS *vars = ERR_VARS(error);
    if (Is_Nulled(&vars->nearest))  // only update if it doesn't have it [1]
        Update_Error_Near_For_Line(
            error, transcode, transcode->line, transcode->line_head
        );
    return Native_Fail_Result(level_, error);
}

#undef FAIL
#define FAIL(p) \
    Scanner_Fail_Helper(transcode, level_, Derive_Error_From_Pointer(p))


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
// path to replace it in the scan of the array the path is in.
//
Bounce Scanner_Executor(Level* const L) {
    USE_LEVEL_SHORTHANDS (L);

    if (THROWING)
        return THROWN;  // no state to cleanup (just data stack, auto-cleaned)

  #if RUNTIME_CHECKS
    char scan_mode = LEVEL_STATE_BYTE(L);  // to see in C debug watchlist
    if (scan_mode == '/' or scan_mode == ':' or scan_mode == '.')
        assert(Get_Scan_Executor_Flag(L, INTERSTITIAL_SCAN));
    else
        assert(Not_Scan_Executor_Flag(L, INTERSTITIAL_SCAN));
    USED(scan_mode);
  #endif

    ScanState* S = &level_->u.scan;
    TranscodeState* transcode = S->transcode;

    DECLARE_MOLDER (mo);

    switch (STATE) {
      case ST_SCANNER_OUTERMOST_SCAN:
      case ST_SCANNER_TUPLE_MODE:
      case ST_SCANNER_PATH_MODE:
      case ST_SCANNER_CHAIN_MODE:
      case ST_SCANNER_GROUP_MODE:
      case ST_SCANNER_BLOCK_MODE:
      case ST_SCANNER_FENCE_MODE:  // coming soon!
        if (L == TOP_LEVEL)
            goto initial_entry;  // assume no sublevel means initial entry
        assert(TOP_LEVEL->prior = L);
        goto child_array_scanned;

      default:
        assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    assert(S->num_quotes_pending == 0);
    assert(not S->sigil_pending);
    assert(not S->quasi_pending);

} loop: {  //////////////////////////////////////////////////////////////////

    Token token;

  blockscope {
    assert(mo->string == nullptr);  // pushed mold should have been handled
    Option(Error*) error = Trap_Locate_Token_May_Push_Mold(&token, mo, L);
    if (error)
        return RAISE(unwrap error);
  }

    if (token == TOKEN_END) {  // reached '\0'
        //
        // If we were scanning a BLOCK! or a GROUP!, then we should have hit
        // an ending `]` or `)` and jumped to `done`.  If an end token gets
        // hit first, there was never a proper closing.
        //
        if (Is_List_Scan(L))
            return RAISE(Error_Missing(S, STATE));

        Option(Error*) error = Trap_Flush_Pending_Sigils(S);
        if (error)
            return RAISE(unwrap error);

        goto done;
    }

    assert(S->begin and S->end and S->begin < S->end);

    REBLEN len = S->end - S->begin;

    transcode->at = S->end;  // accept token, may adjust below if token "grows"

    switch (token) {
      case TOKEN_NEWLINE: {
        Option(Error*) error = Trap_Flush_Pending_Sigils(S);
        if (error)
            return RAISE(unwrap error);

        Set_Scan_Executor_Flag(L, NEWLINE_PENDING);
        transcode->line_head = transcode->at;

        if (not Is_Interstitial_Scan(L))
            goto loop;
        break; }

      case TOKEN_BLANK:
        assert(*S->begin == '_' and len == 1);
        Init_Blank(PUSH());
        break;

      case TOKEN_COMMA: {
        assert(*S->begin == ',' and len == 1);

        if (*S->end == '~') {
            if (not S->quasi_pending)
                return FAIL("Comma only followed by ~ for ~,~ quasiform");
            Quasify_Isotopic_Fundamental(Init_Comma(PUSH()));
            S->sigil_pending = SIGIL_0;
        }
        else {
            if (S->quasi_pending or S->sigil_pending) {  // ['$, 10] => '$ , 10
                Option(Error*) error = Trap_Flush_Pending_Sigils(S);
                if (error)
                    return RAISE(unwrap error);
            }
            else if (S->num_quotes_pending) {
                // fall through normally, want [', 10] => ', 10
            }
            if (Is_Interstitial_Scan(L)) {
                //
                // We only see a comma during a PATH! or TUPLE! scan in cases
                // where a blank is needed.  So we'll get here with [/a/ , xxx]
                // but won't get here with [/a , xxx].
                //
                // Note that `[/a/, xxx]` will bypass the recursion, so we also
                // only get here if there's space before the comma.
                //
                assert(transcode->at == S->end);  // token was "accepted"
                --transcode->at;  // "unaccept" token so interstitial sees `,`
                goto done;
            }
            Init_Comma(PUSH());
        }
        break; }

      case TOKEN_CARET:
        assert(*S->begin == '^' and len == 1);
        goto token_prefixable_sigil;

      case TOKEN_AT:
        assert(*S->begin == '@' and len == 1);
        goto token_prefixable_sigil;

      case TOKEN_AMPERSAND:
        assert(*S->begin == '&' and len == 1);
        goto token_prefixable_sigil;

      case TOKEN_DOLLAR:
        assert(*S->begin == '$' and len == 1);
        goto token_prefixable_sigil;

      token_prefixable_sigil:
        if (S->sigil_pending)
            return RAISE(Error_Syntax(S, token));  // no "GET-GET-WORD!"

        S->sigil_pending = Sigil_From_Token(token);
        goto loop_if_next_token_modifiable;

      case TOKEN_WORD:
        assert(len != 0);
        Init_Word(PUSH(), Intern_UTF8_Managed(S->begin, len));
        break;

      case TOKEN_ISSUE: {
        Size mold_size = String_Size(mo->string) - mo->base.size;
        Length mold_len = String_Len(mo->string) - mo->base.index;
        Utf8(const*) utf8 = Binary_At(mo->string, mo->base.size);

        if (mold_size == 0) {
            assert(mold_len == 0);
            Init_Space(PUSH());  // !!! can't discern #"", for now
        }
        else  // small strings fit in cell
            Init_Issue(PUSH(), utf8, mold_size, mold_len);

        Drop_Mold(mo);
        break; }

      case TOKEN_APOSTROPHE: {
        assert(*S->begin == '\'');  // should be `len` sequential apostrophes

        if (S->sigil_pending)  // can't do @'foo: or :'foo or ~'foo~
            return RAISE(Error_Syntax(S, token));

        S->num_quotes_pending = len;  // apply quoting to next token
        goto loop_if_next_token_modifiable; }

      case TOKEN_TILDE: {
        assert(*S->begin == '~' and len == 1);

        if (S->quasi_pending or S->sigil_pending)  // no @~foo:~ or ~~foo~~
            return RAISE(Error_Syntax(S, token));

        S->quasi_pending = true;  // apply to next token
        goto loop_if_next_token_modifiable; }

    // R3-Alpha's scanner was not designed to give back TOKEN_WHITESPACE, so
    // if you are processing tokens and don't in that moment look ahead for
    // for whitespace, the opportunity is lost.
    //
    // Let's say you have something like:
    //
    //     a/b/' c/d
    //
    // Then if the apostrophe pushes a quote pending, the next thing you will
    // get is a TOKEN_WORD for "c".  This creates some pain, but it's likely
    // more optimal to force "potentially-modifying tokens" to do a slight
    // amount of lookahead than to introduce TOKEN_WHITESPACE.

      loop_if_next_token_modifiable: {
        if (not Is_Lex_Whitespace(*S->end) and not Is_Lex_End_List(*S->end))
            goto loop;
        Option(Error*) error = Trap_Flush_Pending_Sigils(S);
        if (error)
            return RAISE(unwrap error);
        break; }

      case TOKEN_FENCE_BEGIN:
      case TOKEN_GROUP_BEGIN:
      case TOKEN_BLOCK_BEGIN: {
        Byte mode;
        switch (token) {
          case TOKEN_FENCE_BEGIN:
            mode = ST_SCANNER_FENCE_MODE;
            break;
          case TOKEN_GROUP_BEGIN:
            mode = ST_SCANNER_GROUP_MODE;
            break;
          case TOKEN_BLOCK_BEGIN:
            mode = ST_SCANNER_BLOCK_MODE;
            break;
          default:
            panic (L);
        }

        Level* sub = Make_Scan_Level(
            transcode,
            L->feed,
            LEVEL_FLAG_TRAMPOLINE_KEEPALIVE  // we want accrued stack
                | (L->flags.bits & SCAN_EXECUTOR_MASK_RECURSE)
                | LEVEL_FLAG_RAISED_RESULT_OK
                | FLAG_STATE_BYTE(mode)
        );
        Push_Level_Erase_Out_If_State_0(OUT, sub);
        return CONTINUE_SUBLEVEL(sub); }

      case TOKEN_TUPLE:
        assert(*S->begin == '.' and len == 1);
        goto out_of_turn_interstitial;

      case TOKEN_CHAIN:
        assert(*S->begin == ':' and len == 1);
        goto out_of_turn_interstitial;

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
        //
        if (S->quasi_pending) {
            Init_Trash(PUSH());  // if we end up with ~/~, we decay it to word
            S->quasi_pending = 0;  // quasi-sequences don't exist
        }
        else
            Init_Blank(PUSH());

        assert(transcode->at == S->end);
        transcode->at = S->begin;  // "unconsume" .` or `/` or `:` token
        break; }

      case TOKEN_BLOCK_END:
        assert(*S->begin == ']' and len == 1);
        goto handle_list_end_delimiter;

      case TOKEN_GROUP_END:
        assert(*S->begin == ')' and len == 1);
        goto handle_list_end_delimiter;

      case TOKEN_FENCE_END:
        assert(*S->begin == '}' and len == 1);
        goto handle_list_end_delimiter;

      handle_list_end_delimiter: {
        Option(Error*) error = Trap_Flush_Pending_Sigils(S);
        if (error)
            return RAISE(unwrap error);

        Byte end_delimiter = *S->begin;
        if (Scan_Mode_Matches(L, end_delimiter))
            goto done;

        if (Is_Interstitial_Scan(L)) {  // implicit end [the /] (abc/)
            assert(transcode->at == S->end);  // falsely accepted end_delimiter
            --transcode->at;  // unaccept, and end the interstitial scan first
            goto done;
        }

        if (Is_List_Scan(L))  // expected ']' before ')' or vice-versa
            return RAISE(Error_Mismatch(STATE, end_delimiter));

        return RAISE(Error_Extra(end_delimiter)); }

      case TOKEN_INTEGER: {
        //
        // We treat `10.20.30` as a TUPLE!, but `10.20` has a cultural lock on
        // being a DECIMAL! number.  Due to the overlap, Locate_Token() does
        // not have enough information in hand to discern TOKEN_DECIMAL; it
        // just returns TOKEN_INTEGER and the decision is made here.
        //
        // (Imagine we're in a tuple scan and INTEGER! 10 was pushed, and are
        // at "20.30" in the 10.20.30 case.  Locate_Token() would need access
        // to the scan mode to know that the tuple scan was happening, else
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
            (
                *S->end == '.'
                or *S->end == ','  // still allow `1,2` as `1.2` synonym
            )
            and not Is_Interstitial_Scan(L)  // not in PATH!/TUPLE! (yet)
            and Is_Lex_Number(S->end[1])  // If # digit, we're seeing `###.#???`
        ){
            // If we will be scanning a TUPLE!, then we're at the head of it.
            // But it could also be a DECIMAL! if there aren't any more dots.
            //
            const Byte* ep = S->end + 1;
            for (; *ep != '.'; ++ep) {
                if (Is_Lex_Delimit(*ep)) {
                    token = TOKEN_DECIMAL;
                    S->end = ep;  // extend token
                    len = S->end - S->begin;
                    transcode->at = S->end;  // "accept" extended token
                    goto scan_decimal;
                }
            }
        }

        // Wasn't beginning of a DECIMAL!, so scan as a normal INTEGER!
        //
        if (S->end != Try_Scan_Integer_To_Stack(S->begin, len))
            return RAISE(Error_Syntax(S, token));

        break; }

      case TOKEN_DECIMAL:
      case TOKEN_PERCENT:
      scan_decimal:
        if (Is_Lex_Interstitial(*S->end)) {
            ++S->end;  // include / in error
            return RAISE(Error_Syntax(S, token));  // No `1.2/abc`
        }
        if (S->end != Try_Scan_Decimal_To_Stack(S->begin, len, false))
            return RAISE(Error_Syntax(S, token));

        if (S->begin[len - 1] == '%')
            HEART_BYTE(TOP) = TYPE_PERCENT;
        break;

      case TOKEN_MONEY:
        if (Is_Lex_Interstitial(*S->end)) {  // Do not allow $1/$2
            ++S->end;  // include / in error message
            return RAISE(Error_Syntax(S, token));
        }
        if (S->end != Try_Scan_Money_To_Stack(S->begin, len))
            return RAISE(Error_Syntax(S, token));
        break;

      case TOKEN_TIME:
        if (S->end != Try_Scan_Time_To_Stack(S->begin, len))
            return RAISE(Error_Syntax(S, token));
        break;

      case TOKEN_DATE: {
        const Byte* ep = S->end;
        while (*ep == '/' and not Scan_Mode_Matches(L, '/')) {  // Is date/time?
            ep++;
            while (*ep == '.' or *ep == ':' or Is_Lex_Not_Delimit(*ep))
                ++ep;
            len = ep - S->begin;
            if (len > 50)  // prevent infinite loop, should never be longer
                break;
            S->end = ep;
        }
        if (S->end != Try_Scan_Date_To_Stack(S->begin, len))
            return RAISE(Error_Syntax(S, token));
        transcode->at = S->end;  // consume extended token
        break; }

      case TOKEN_CHAR: {  // now just "issue enclosed in quotes"
        Init_Issue(
            PUSH(),
            cast(Utf8(const*), Binary_At(mo->string, mo->base.size)),
            String_Size(mo->string) - mo->base.size,
            String_Len(mo->string) - mo->base.index
        );
        Drop_Mold(mo);
        break; }

      case TOKEN_STRING: {  // UTF-8 pre-scanned above, and put in mold buffer
        String* s = Pop_Molded_String(mo);
        Init_Text(PUSH(), s);
        break; }

      case TOKEN_BINARY:
        if (S->end != Try_Scan_Binary_To_Stack(S->begin, len))
            return RAISE(Error_Syntax(S, token));
        break;

      case TOKEN_PAIR:
        if (S->end != Try_Scan_Pair_To_Stack(S->begin, len))
            return RAISE(Error_Syntax(S, token));
        break;

      case TOKEN_FILE: {
        String* s = Pop_Molded_String(mo);
        Init_File(PUSH(), s);
        break; }

      case TOKEN_EMAIL:
        if (S->end != Try_Scan_Email_To_Stack(S->begin, len))
            return RAISE(Error_Syntax(S, token));
        break;

      case TOKEN_URL:
        if (S->end != Try_Scan_URL_To_Stack(S->begin, len))
            return RAISE(Error_Syntax(S, token));
        break;

      case TOKEN_TAG: {
        assert(
            len >= 2 and *S->begin == '<'
            /* and *S->end == '>' */  // !!! doesn't know, scan ignores length
        );

        Size size = len - 2;  // !!! doesn't know where tag actually ends (?)
        String* s = Append_UTF8_May_Fail(
            nullptr,
            cs_cast(S->begin + 1),
            size,
            STRMODE_NO_CR
        );

        if (S->end - 1 != S->begin + 1 + size)
            return RAISE(Error_Syntax(S, token));

        Init_Any_String(PUSH(), TYPE_TAG, s);
        break; }

      case TOKEN_CONSTRUCT: {
        Level* sub = Make_Scan_Level(
            transcode,
            L->feed,
            LEVEL_FLAG_TRAMPOLINE_KEEPALIVE  // we want accrued stack
                | (L->flags.bits & SCAN_EXECUTOR_MASK_RECURSE)
                | LEVEL_FLAG_RAISED_RESULT_OK
                | FLAG_STATE_BYTE(ST_SCANNER_BLOCK_MODE)
        );

        Push_Level_Erase_Out_If_State_0(OUT, sub);  // do stackful, for now

        bool threw = Trampoline_With_Top_As_Root_Throws();

        if (threw) {
            Drop_Level(sub);  // should not have accured stack if threw
            return FAIL(Error_No_Catch_For_Throw(L));
        }

        if (Is_Raised(OUT)) {
            Drop_Level(sub);
            return OUT;
        }

        goto construct_scan_to_stack_finished; }

      case TOKEN_END:  // handled way above, before the switch()
      default:
        panic ("Invalid TOKEN in Scanner.");
    }

} lookahead_for_sequencing_character: {  /////////////////////////////////////

  // Quasiforms are currently legal in PATH!/CHAIN!/TUPLE!.  There's not a
  // particularly great reason as to why...it's just that `~/foo/bar.txt` is
  // a very useful path form (more useful than quasipaths and antituples).
  // Given that we know tildes in paths don't mean the path itself is a
  // quasiform, we are able to unambiguously interpret `~abc~.~def~` or
  // similar.  It may be useful, so enabling it for now.

    if (S->quasi_pending) {
        if (*transcode->at != '~')
            return RAISE(Error_Syntax(S, TOKEN_TILDE));

        Option(Error*) e = Trap_Coerce_To_Quasiform(TOP_ELEMENT);
        if (e)
            return RAISE(unwrap e);

        ++transcode->at;  // must compensate the `transcode->at = S->end`
        S->quasi_pending = false;
    }

    // At this point the item at TOP is the last token pushed.  It has
    // not had any `sigil_pending` or `num_quotes_pending` applied...so when
    // processing something like `'$foo/bar` on the first step we'd only see
    // `foo` pushed.  This is the point where we look for the `/` or `.`
    // to either start or continue a tuple or path.
    //
    // If we're starting a path or continuing, we want to wait to apply the
    // sigils and quotes.  Perhaps obviously, with $a/b the $ does not go on
    // the "a", but on the "a/b".  But also, with a/$b.c the $ does not go
    // on the "b", it goes on "b.c", which is why we do not apply any sigils
    // or quotes until a sequence (or subsequence) scan is complete.

    if (Is_Interstitial_Scan(L)) {  // adding to existing path/chain/tuple
        if (STATE == *transcode->at) {
            Option(Error*) error = Trap_Apply_Pending_Decorations(
                S, TOP_ELEMENT
            );
            if (error)
                return RAISE(unwrap error);

            ++transcode->at;  // consume the matching interstitial delimiter

            if (
                Is_Lex_Whitespace(*transcode->at)
                or Is_Lex_End_List(*transcode->at)
                or *transcode->at == ','
                or *transcode->at == ';'
            ){
                Init_Blank(PUSH());
                goto done;
            }

            goto loop;
        }

        if (Is_Lex_Interstitial(*transcode->at)) {
            if (Is_Lex_Sub_Interstitial(L, *transcode->at))
                goto scan_sequence_top_is_head;  // foo:bar.

            // Here, consider something like "/foo:bar/" or "foo:bar/" where
            // we are in a lower precedence interstitial scan mode (:) and
            // encounter a higher priority interstitial character that
            // may-or-may-not be part of an existing interstitial scan.  We
            // don't know which it is, so we finish out our scan level and
            // leave `transcode->at` where it is, deferring to higher levels.
            //
            goto done;  //  for instance foo.bar: must end the tuple scan
        }

        if (
            Is_Lex_Whitespace(*transcode->at)
            or Is_Lex_End_List(*transcode->at)
            or *transcode->at == ','
            or *transcode->at == ';'
        ){
            Option(Error*) error = Trap_Apply_Pending_Decorations(
                S, TOP_ELEMENT
            );
            if (error)
                return RAISE(unwrap error);
            goto done;
        }

        return RAISE("Malformed sequence scan...something like a.b()");
    }
    else if (Is_Lex_Interstitial(*transcode->at)) {  // a new path/chain/tuple
        //
        // We're noticing a sequence was actually starting with the element
        // that just got pushed, so it should be a part of that path.
        //
        goto scan_sequence_top_is_head;
    }

  //=//// APPLY PENDING SIGILS AND QUOTES (NON-INTERSTITIAL) //////////////=//

    assert(not Is_Interstitial_Scan(L));

    // 2. Set the newline on the new value, indicating molding should put a
    //    line break *before* this value (needs to be done after recursion to
    //    process paths or other arrays...because the newline belongs on the
    //    whole array...not the first element of it).

    Option(Error*) error = Trap_Apply_Pending_Decorations(
        S, TOP_ELEMENT
    );
    if (error)
        return RAISE(unwrap error);

    if (Get_Scan_Executor_Flag(L, NEWLINE_PENDING)) {
        Clear_Scan_Executor_Flag(L, NEWLINE_PENDING);
        Set_Cell_Flag(TOP, NEWLINE_BEFORE);  // must do after recursion [2]
    }

    if (Get_Scan_Executor_Flag(L, JUST_ONCE)) {  // e.g. TRANSCODE:NEXT
        goto done;
    }

    goto loop;

} child_array_scanned: {  ////////////////////////////////////////////////////

    if (Is_Raised(OUT)) {
        Drop_Level(SUBLEVEL);
        return OUT;
    }

    Heart heart;
    switch (LEVEL_STATE_BYTE(SUBLEVEL)) {
      case ST_SCANNER_BLOCK_MODE:
        heart = TYPE_BLOCK;
        break;
      case ST_SCANNER_FENCE_MODE:
        heart = TYPE_FENCE;
        break;
      case ST_SCANNER_GROUP_MODE:
        heart = TYPE_GROUP;
        break;
      default:
        panic (L);
    }

    Level* sub = SUBLEVEL;

    if (Get_Scan_Executor_Flag(L, SAVE_LEVEL_DONT_POP_ARRAY)) {  // see flag
        if (*transcode->at != STATE)
            return FAIL("Delimiters malformed in interpolation");
        ++transcode->at;

        assert(sub->prior == L);  // sanity check
        g_ts.top_level = sub->prior;
        sub->prior = transcode->saved_levels;
        transcode->saved_levels = sub;
        goto done;  // skip over the pop, leave elements on stack
    }

    Source* a = Pop_Managed_Source_From_Stack(sub->baseline.stack_base);
    if (Get_Scan_Executor_Flag(sub, NEWLINE_PENDING))
        Set_Source_Flag(a, NEWLINE_AT_TAIL);

    Drop_Level(sub);

    // Tag array with line where the beginning bracket/group/etc. was found
    //
    MISC_SOURCE_LINE(a) = transcode->line;
    Tweak_Link_Filename(a, maybe transcode->file);

    Init_Any_List(PUSH(), heart, a);

    if (transcode->at == nullptr)
        goto done;

    goto lookahead_for_sequencing_character;

} scan_sequence_top_is_head: { ///////////////////////////////////////////////

    Heart heart;
    Byte sub_mode = *transcode->at;

    switch (sub_mode) {
      case '/':
        assert(not Scan_Mode_Matches(L, '/'));  // should have continued
        heart = TYPE_PATH;
        break;

      case ':':
        assert(not Scan_Mode_Matches(L, ':'));  // should have continued
        heart = TYPE_CHAIN;
        break;

      case '.':
        assert(not Scan_Mode_Matches(L, '.'));  // should have continued
        heart = TYPE_TUPLE;
        break;

      default:
        panic (nullptr);
    }

    ++transcode->at;

    StackIndex stackindex_path_head = TOP_INDEX;

    if (
        Is_Lex_Whitespace(*transcode->at)  // `foo/baz/ bar` (includes '\0')
        or Is_Lex_End_List(*transcode->at)  // `foo/baz/]`
        or *transcode->at == ';'  // `foo/;bar`
        or *transcode->at == ','  // `a:, b`
    ){
        // If there's nothing to recursively scan, we could end up with an
        // array that's too short.  This isn't just an optimization: due to
        // the lack of a TOKEN_WHITESPACE we really have to take action now,
        // because if we sub-scanned we'd not be able to tell when adding
        // a blank to the tail was appropriate.
        //
        Init_Blank(PUSH());
    }
    else {
        Level* sub = Make_Scan_Level(
            transcode,
            L->feed,
            LEVEL_FLAG_RAISED_RESULT_OK
                | FLAG_STATE_BYTE(sub_mode)
                | SCAN_EXECUTOR_FLAG_INTERSTITIAL_SCAN
        );
        Push_Level_Erase_Out_If_State_0(OUT, sub);

        bool threw = Trampoline_With_Top_As_Root_Throws();

        Drop_Level_Unbalanced(sub);  // allow stack accrual

        if (threw)  // automatically drops failing stack before throwing
            return FAIL(Error_No_Catch_For_Throw(L));

        if (Is_Raised(OUT)) {  // no auto-drop without `return RAISE()`
            Drop_Data_Stack_To(STACK_BASE);
            return OUT;
        }
    }

    // Run through the generalized pop path code, which does any
    // applicable compression...and validates the array.
    //
    DECLARE_VALUE (temp);

    // !!! The scanner needs an overhaul and rewrite to be less ad hoc.
    // Right now, dots act as delimiters for tuples which messes with
    // email addresses that contain dots.  It isn't obvious how to
    // patch support in for that, but we can notice when a tuple tries
    // to be made with an email address in it (which is not a legal
    // tuple) and mutate that into an email address.  Clearly this is
    // bad, but details of scanning isn't the focus at this time.
    //
    if (sub_mode == ST_SCANNER_TUPLE_MODE) {
        bool any_email = false;
        StackIndex stackindex = TOP_INDEX;
        for (; stackindex != stackindex_path_head - 1; --stackindex) {
            if (Is_Email(Data_Stack_At(Element, stackindex))) {
                if (any_email)
                    return RAISE(Error_Syntax(S, TOKEN_TUPLE));
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
            DECLARE_ATOM (items);
            Init_Any_List(
                items,
                TYPE_THE_BLOCK,  // don't want to evaluate
                Pop_Source_From_Stack(stackindex_path_head - 1)
            );
            Push_Lifeguard(items);
            Value* email = rebValue("as email! delimit -{.}-", items);
            Drop_Lifeguard(items);
            Copy_Cell(temp, email);
            rebRelease(email);
            goto push_temp;
        }
    }

  blockscope {  // gotos would cross this initialization without
    Option(Error*) error = Trap_Pop_Sequence_Or_Conflation(
        temp,  // doesn't write directly to stack since popping stack
        heart,
        stackindex_path_head - 1
    );
    if (error)
        return RAISE(unwrap error);
  }

    assert(
        Is_Quasi_Word(temp)     // [~ ~] => ~.~ or ~/~
        or Is_Word(temp)        // [_ _] => . or /
        or Is_Time(temp)        // [12 34] => 12:34
        or Any_Sequence(temp)
    );

  push_temp:
    Copy_Cell(PUSH(), temp);

    // Can only store file and line information if it has an array
    //
    if (
        Cell_Has_Node1(TOP)
        and CELL_NODE1(TOP) != nullptr  // null legal in node slots ATM
        and not Is_Node_A_Cell(CELL_NODE1(TOP))
        and Is_Stub_Source(cast(Stub*, CELL_NODE1(TOP)))
    ){
        Source* a = cast(Source*, CELL_SERIESLIKE_NODE(TOP));
        MISC_SOURCE_LINE(a) = transcode->line;
        Tweak_Link_Filename(a, maybe transcode->file);
    }

    if (transcode->at == nullptr)  // reached e.g. with a/'
        goto done;

    goto lookahead_for_sequencing_character;

} construct_scan_to_stack_finished: {  ///////////////////////////////////////

    Source* array = Pop_Managed_Source_From_Stack(SUBLEVEL->baseline.stack_base);
    if (Get_Scan_Executor_Flag(L, NEWLINE_PENDING))
        Set_Source_Flag(array, NEWLINE_AT_TAIL);

    Drop_Level(SUBLEVEL);

    // Tag array with line where the beginning bracket/group/etc. was found
    //
    MISC_SOURCE_LINE(array) = transcode->line;
    Tweak_Link_Filename(array, maybe transcode->file);

    if (Array_Len(array) == 0 or not Is_Word(Array_Head(array))) {
        DECLARE_ELEMENT (temp);
        Init_Block(temp, array);
        return RAISE(Error_Malconstruct_Raw(temp));
    }

    if (Array_Len(array) == 1) {
        //
        // #[true] #[false] #[none] #[unset] -- no equivalents.
        //
        DECLARE_ELEMENT (temp);
        Init_Block(temp, array);
        return RAISE(Error_Malconstruct_Raw(temp));
    }
    else if (Array_Len(array) == 2) {  // #[xxx! [...]], #[xxx! yyy!], etc.
        //
        // !!! At one time, Ren-C attempted to merge "construction syntax"
        // with MAKE, so that `#[xxx! [...]]` matched `make xxx! [...]`.
        // But the whole R3-Alpha concept was flawed, as round-tripping
        // structures neglected binding...and the scanner is just supposed
        // to be making a data structure.
        //
        // Hence this doesn't really work in any general sense.

        return FAIL("#[xxx! [...]] construction syntax no longer supported");
    }
    else {
        DECLARE_ELEMENT (temp);
        Init_Block(temp, array);
        return RAISE(Error_Malconstruct_Raw(temp));
    }

    goto lookahead_for_sequencing_character;

} done: {  ///////////////////////////////////////////////////////////////////

    assert(mo->string == nullptr);  // mold should have been handled

    assert(S->num_quotes_pending == 0);
    assert(not S->sigil_pending);
    assert(not S->quasi_pending);

    // Note: ss->newline_pending may be true; used for ARRAY_NEWLINE_AT_TAIL

    return VOID;

}}


//=//// UNDEFINE THE AUGMENTED SCANNER RAISE AND FAIL /////////////////////=//

#undef RAISE
#define RAISE(p) \
    Native_Raised_Result(level_, Derive_Error_From_Pointer(p))

#undef FAIL
#define FAIL(p) \
    (Fail_Prelude_File_Line_Tick(__FILE__, __LINE__, TICK), \
            Native_Fail_Result(level_, Derive_Error_From_Pointer(p)))


//
//  Scan_UTF8_Managed: C
//
// This is a "stackful" call that takes a buffer of UTF-8 and will try to
// scan it into an array, or raise an "abrupt" error (that won't be catchable
// by things like ATTEMPT or EXCEPT, only RESCUE).
//
// 1. This routine doesn't offer parameterization for variadic "splicing" of
//    already-loaded values mixed with the textual code as it's being
//    scanned.  (For that, see `rebTranscodeInto()`.)  But the underlying
//    scanner API requires a variadic feed to be provided...so we just pass
//    a simple 2-element feed in of [UTF-8 string, END]
//
// 2. This uses the "C++ form" of variadic, where it packs the elements into
//    an array, vs. using the va_arg() stack.  So vaptr is nullptr to signal
//    the `p` pointer is this packed array, vs. the first item of a va_list.)
//
Source* Scan_UTF8_Managed(
    Option(const String*) file,
    const Byte* utf8,
    Size size
){
    assert(utf8[size] == '\0');
    UNUSED(size);  // scanner stops at `\0` (no size limit functionality)

    const void* packed[2] = {utf8, rebEND};  // BEWARE: Stack, can't trampoline!
    Feed* feed = Make_Variadic_Feed(  // scanner requires variadic [1]
        packed, nullptr,  // va_list* as nullptr means `p` is packed [2]
        FEED_MASK_DEFAULT
    );
    Add_Feed_Reference(feed);
    Sync_Feed_At_Cell_Or_End_May_Fail(feed);

    StackIndex base = TOP_INDEX;
    while (Not_Feed_At_End(feed)) {
        Derelativize(PUSH(), At_Feed(feed), Feed_Binding(feed));
        Fetch_Next_In_Feed(feed);
    }
    // Note: exhausting feed should take care of the va_end()

    Release_Feed(feed);  // feeds are dynamically allocated and must be freed

    Source* a = Pop_Managed_Source_From_Stack(base);
    /* Set_Source_Flag(a, NEWLINE_AT_TAIL); */  // what heuristics for this?

    MISC_SOURCE_LINE(a) = 1;
    Tweak_Link_Filename(a, maybe file);

    return a;
}


//
//  Startup_Scanner: C
//
void Startup_Scanner(void)
{
    REBLEN n = 0;
    while (g_token_names[n])
        ++n;
    assert(cast(Token, n) == MAX_TOKEN + 1);

    // The details of what ASCII characters must be percent encoded
    // are contained in RFC 3896, but a summary is here:
    //
    // https://stackoverflow.com/a/7109208/
    //
    // Everything but: A-Z a-z 0-9 - . _ ~ : / ? # [ ] @ ! $ & ' ( ) * + , ; =
    //
  #if RUNTIME_CHECKS  // we use g_lex_map for speed, but double check
    const char *no_encode =
        "ABCDEFGHIJKLKMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789" \
            "-._~:/?#[]@!$&'()*+,;=";

    Byte b;
    for (b = 0x01; b < 0x80; ++b) {  // don't call on NUL or non-ASCII
        bool needs_encoding = (strchr(no_encode, b) == nullptr);
        assert(needs_encoding == Ascii_Char_Needs_Percent_Encoding(b));
    }
  #endif
}


//
//  Shutdown_Scanner: C
//
void Shutdown_Scanner(void)
{
}


//
//  Trap_Transcode_One: C
//
// This is a generic helper that powers things like (to integer! "1020").
//
// For now we implement it inefficiently, but it should be done without
// needing to call a native.
//
Option(Error*) Trap_Transcode_One(
    Sink(Element) out,
    Option(Heart) heart,
    const Element* any_utf8
){
    assert(Any_Utf8(any_utf8));  // use rebQ(), as SIGIL!, WORD!, evaluative
    Value* trapped = rebEntrap("transcode:one as text!", rebQ(any_utf8));
    if (Is_Error(trapped)) {
        Error* error = Cell_Error(trapped);
        rebRelease(trapped);
        return error;
    }
    Meta_Unquotify_Known_Stable(trapped);
    if (heart and Heart_Of(trapped) != heart) {
        rebRelease(trapped);
        return Error_User("Trap_Transcode_One() gave unwanted type");
    }
    Copy_Cell(out, cast(Element*, trapped));
    rebRelease(trapped);
    return SUCCESS;
}


//
//  transcode: native [
//
//  "Translates UTF-8 source (from a text or binary) to Rebol elements"
//
//      return: "Transcoded elements block, or ~[remainder element]~ if /NEXT"
//          [~null~ block! ~[[text! blob!] element?]~ element?]
//      source "If BINARY!, must be UTF-8 encoded"
//          [any-utf8? blob!]
//      :next "Translate one element and give back next position"
//      :one "Transcode one element and return it"
//      :file "File to be associated with BLOCK!s and GROUP!s in source"
//          [file! url!]
//      :line "Line number for start of scan, word variable will be updated"
//          [integer! any-word?]
//      <local> buffer
//  ]
//
DECLARE_NATIVE(TRANSCODE)
{
    INCLUDE_PARAMS_OF_TRANSCODE;

    Element* source = Element_ARG(SOURCE);

    Size size;
    const Byte* bp = Cell_Bytes_At(&size, source);

    TranscodeState* transcode;
    Value* transcode_buffer = LOCAL(BUFFER);  // kept as a BLOB!, gets GC'd

    enum {
        ST_TRANSCODE_INITIAL_ENTRY = STATE_0,
        ST_TRANSCODE_SCANNING,
        ST_TRANSCODE_ENSURE_NO_MORE
    };

    switch (STATE) {
      case ST_TRANSCODE_INITIAL_ENTRY:
        goto initial_entry;

      case ST_TRANSCODE_SCANNING:
        transcode = cast(
            TranscodeState*,
            Binary_Head(Cell_Binary_Known_Mutable(transcode_buffer))
        );
        goto scan_to_stack_maybe_failed;

      case ST_TRANSCODE_ENSURE_NO_MORE:
        if (not Is_Raised(OUT)) {  // !!! return this error, or new one?
            if (TOP_INDEX == STACK_BASE + 1)  // didn't scan anything else
                Move_Drop_Top_Stack_Element(OUT);
            else {  // scanned another item, we only wanted one!
                assert(TOP_INDEX == STACK_BASE + 2);
                Drop_Data_Stack_To(STACK_BASE);
                Init_Error(
                    OUT,
                    Error_User("TRANSCODE:ONE scanned more than one element")
                );
                Raisify(OUT);
            }
        }
        Drop_Level(SUBLEVEL);
        return OUT;

      default:
        assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

  // 1. Though all BLOB! leave a spare byte at the end in case they are
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

    if (Is_Blob(source))  // scanner needs data to end in '\0' [1]
        Term_Binary(m_cast(Binary*, Cell_Binary(source)));

    Option(const String*) file;
    if (Bool_ARG(FILE)) {
        file = Cell_String(ARG(FILE));
        Freeze_Flex(unwrap file);  // freezes vs. interning [2]
    }
    else
        file = ANONYMOUS;

    Value* line_number = stable_SCRATCH;  // use as scratch space
    if (Any_Word(ARG(LINE)))
        Get_Var_May_Fail(
            line_number,
            Element_ARG(LINE),
            SPECIFIED
        );
    else
        Copy_Cell(line_number, ARG(LINE));

    LineNumber start_line;
    if (Is_Nulled(line_number)) {
        start_line = 1;
    }
    else if (Is_Integer(line_number)) {
        start_line = VAL_INT32(line_number);
        if (start_line <= 0)
            return FAIL(PARAM(LINE));  // definitional?
    }
    else
        return FAIL(":LINE must be INTEGER! or an ANY-WORD? integer variable");

    // Because we're building a frame, we can't make a {bp, END} packed array
    // and start up a variadic feed...because the stack variable would go
    // bad as soon as we yielded to the trampoline.  Have to use an END feed
    // and preload the transcode->at of the scanner here.
    //
    // Note: Could reuse global TG_End_Feed if context was null.

    Feed* feed = Make_Array_Feed_Core(EMPTY_ARRAY, 0, SPECIFIED);

    Flags flags =
        LEVEL_FLAG_TRAMPOLINE_KEEPALIVE  // query pending newline
        | LEVEL_FLAG_RAISED_RESULT_OK  // want to pass on definitional error
        | FLAG_STATE_BYTE(ST_SCANNER_OUTERMOST_SCAN);

    if (Bool_ARG(NEXT) or Bool_ARG(ONE))
        flags |= SCAN_EXECUTOR_FLAG_JUST_ONCE;

    Binary* bin = Make_Binary(sizeof(TranscodeState));
    transcode = cast(TranscodeState*, Binary_Head(bin));
    Init_Transcode(transcode, file, start_line, bp);
    Term_Binary_Len(bin, sizeof(TranscodeState));
    Init_Blob(transcode_buffer, bin);

    UNUSED(size);  // currently we don't use this information

    Level* sub = Make_Scan_Level(transcode, feed, flags);

    Push_Level_Erase_Out_If_State_0(OUT, sub);
    STATE = ST_TRANSCODE_SCANNING;
    return CONTINUE_SUBLEVEL(sub);

} scan_to_stack_maybe_failed: {  /////////////////////////////////////////////

    // If the source data bytes are "1" then the scanner will push INTEGER! 1
    // if the source data is "[1]" then the scanner will push BLOCK! [1]
    //
    // Return a block of the results, so [1] and [[1]] in those cases.

    if (Is_Raised(OUT)) {
        Drop_Level(SUBLEVEL);
        return OUT;  // the raised error
    }

    if (Bool_ARG(ONE)) {  // want *exactly* one element
        if (TOP_INDEX == STACK_BASE)
            return RAISE("Transcode was empty (or all comments)");
        assert(TOP_INDEX == STACK_BASE + 1);
        STATE = ST_TRANSCODE_ENSURE_NO_MORE;
        return CONTINUE_SUBLEVEL(SUBLEVEL);
    }

    if (Bool_ARG(NEXT)) {
        if (TOP_INDEX == STACK_BASE)
            Init_Nulled(OUT);
        else {
            assert(TOP_INDEX == STACK_BASE + 1);
            Move_Drop_Top_Stack_Element(OUT);
        }
    }
    else {
        Source* a = Pop_Managed_Source_From_Stack(STACK_BASE);
        if (Get_Scan_Executor_Flag(SUBLEVEL, NEWLINE_PENDING))
            Set_Source_Flag(a, NEWLINE_AT_TAIL);

        MISC_SOURCE_LINE(a) = transcode->line;
        Tweak_Link_Filename(a, maybe transcode->file);

        Init_Block(OUT, a);
    }

    Drop_Level(SUBLEVEL);

    if (Bool_ARG(LINE) and Is_Word(ARG(LINE))) {  // wanted the line number updated
        Element* line_int = Init_Integer(SCRATCH, transcode->line);
        const Element* line_var = Element_ARG(LINE);
        if (Set_Var_Core_Throws(SPARE, nullptr, line_var, SPECIFIED, line_int))
            return THROWN;
    }

    if (not Bool_ARG(NEXT)) {
        assert(Is_Block(OUT));  // should be single block result
        return OUT;
    }

    if (Is_Nulled(OUT))  // no more Elements were left to transcode
        return nullptr;  // must return pure null for THEN/ELSE to work right

    // Return the input BLOB! or TEXT! advanced by how much the transcode
    // operation consumed.
    //
    Element* rest = cast(Element*, SPARE);
    Copy_Cell(rest, source);

    if (Is_Blob(source)) {
        const Binary* b = Cell_Binary(source);
        if (transcode->at)
            VAL_INDEX_UNBOUNDED(rest) = transcode->at - Binary_Head(b);
        else
            VAL_INDEX_UNBOUNDED(rest) = Binary_Len(b);
    }
    else {
        assert(Is_Text(source));

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
        if (transcode->at)
            VAL_INDEX_RAW(rest) += Num_Codepoints_For_Bytes(bp, transcode->at);
        else
            VAL_INDEX_RAW(rest) += Binary_Tail(Cell_String(source)) - bp;
    }

    Source* pack = Make_Source_Managed(2);
    Set_Flex_Len(pack, 2);

    Copy_Meta_Cell(Array_At(pack, 0), rest);
    Copy_Meta_Cell(Array_At(pack, 1), OUT);

    return Init_Pack(OUT, pack);
}}


//
//  Try_Scan_Issue_To_Stack: C
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
Option(const Byte*) Try_Scan_Issue_To_Stack(const Byte* cp, Size size)
{
    const Byte* bp = cp;

    // !!! ISSUE! loading should use the same escaping as FILE!, and have a
    // pre-counted mold buffer, with UTF-8 validation done on the prescan.
    //
    REBLEN len = 0;

    Size n = size;
    while (n > 0) {
        if (not Is_Continuation_Byte(*bp))
            ++len;

        // Allows nearly every visible character that isn't a delimiter
        // as a char surrogate, e.g. #@ is legal, as are #<< and #>>
        // but #/ is a PATH! as is /#
        //
        switch (Get_Lex_Class(*bp)) {
          case LEX_CLASS_DELIMIT:
            return nullptr;  // other purposes, `#(` `#[`, etc.

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
    if (size == 0) {  // plain # is space character, #"" is empty issue
        assert(len == 0);
        Init_Space(PUSH());
    }
    else
        Init_Issue(PUSH(), cast(Utf8(const*), cp), size, len);

    return bp;
}


//
//  Try_Scan_Variadic_Feed_Utf8_Managed: C
//
Option(Source*) Try_Scan_Variadic_Feed_Utf8_Managed(Feed* feed)
//
// 1. We want to preserve CELL_FLAG_FEED_NOTE_META.  This tells us when what
//    the feed sees as an quasiform was really originally intended as an
//    antiform.  The Feed_At() mechanics will typically error on these, but
//    under evaluation the evaluator's treatment of @ will reconstitute the
//    antiform.  (There are various dangers to this, which have not been
//    fully vetted, but the idea is pretty important.)
{
    assert(Detect_Rebol_Pointer(feed->p) == DETECTED_AS_UTF8);

    TranscodeState transcode;
    const LineNumber start_line = 1;
    Init_Transcode(
        &transcode,
        ANONYMOUS,  // %tmp-boot.r name in boot overwritten currently by this
        start_line,
        nullptr  // let scanner fetch feed->p Utf8 as new S->begin
    );

    Flags flags = FLAG_STATE_BYTE(ST_SCANNER_OUTERMOST_SCAN);
    Level* L = Make_Scan_Level(&transcode, feed, flags);

    DECLARE_ATOM (temp);
    Push_Level_Erase_Out_If_State_0(temp, L);
    if (Trampoline_With_Top_As_Root_Throws())
        fail (Error_No_Catch_For_Throw(L));

    if (TOP_INDEX == L->baseline.stack_base) {
        Drop_Level(L);
        return nullptr;
    }

    Source* reified = Pop_Managed_Source_From_Stack(L->baseline.stack_base);
    Drop_Level(L);
    return reified;
}
