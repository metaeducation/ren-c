//
//  File: %sys-scan.h
//  Summary: "Lexical Scanner Definitions"
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

//
//  Tokens returned by the scanner.  Keep in sync with Token_Names[].
//
enum TokenEnum {
    TOKEN_END = 0,
    TOKEN_NEWLINE,
    TOKEN_COMMA,
    TOKEN_BLOCK_END,
    TOKEN_GROUP_END,
    TOKEN_WORD,
    TOKEN_APOSTROPHE,
    TOKEN_BLANK, // not needed
    TOKEN_LOGIC, // not needed
    TOKEN_INTEGER,
    TOKEN_DECIMAL,
    TOKEN_PERCENT,
    TOKEN_MONEY,
    TOKEN_TIME,
    TOKEN_DATE,
    TOKEN_CHAR,
    TOKEN_BLOCK_BEGIN,
    TOKEN_GROUP_BEGIN,
    TOKEN_STRING,
    TOKEN_BINARY,
    TOKEN_PAIR,
    TOKEN_TUPLE,
    TOKEN_CHAIN,
    TOKEN_FILE,
    TOKEN_EMAIL,
    TOKEN_URL,
    TOKEN_ISSUE,
    TOKEN_TAG,
    TOKEN_PATH,
    TOKEN_CONSTRUCT,
    TOKEN_MAX
};
typedef enum TokenEnum Token;

#define KIND_OF_WORD_FROM_TOKEN(t) \
    cast(enum Reb_Kind, REB_WORD + ((t) - TOKEN_WORD))

/*
**  Lexical Table Entry Encoding
*/
#define LEX_SHIFT       5               /* shift for encoding classes */
#define LEX_CLASS       (3<<LEX_SHIFT)  /* class bit field */
#define LEX_VALUE       (0x1F)          /* value bit field */

#define Get_Lex_Class(c)  (g_lex_map[(Byte)c] >> LEX_SHIFT)
#define Get_Lex_Value(c)  (g_lex_map[(Byte)c] & LEX_VALUE)


/*
**  Delimiting Chars (encoded in the LEX_VALUE field)
**  NOTE: Macros do make assumption that _RETURN is the last space delimiter
*/
enum LexDelimitEnum {
    LEX_DELIMIT_SPACE,              // 20 space
    LEX_DELIMIT_END,                // 00 null terminator, end of input
    LEX_DELIMIT_LINEFEED,           // 0A line-feed
    LEX_DELIMIT_RETURN,             // 0D return

    LEX_DELIMIT_MAX_WHITESPACE = LEX_DELIMIT_RETURN,

    LEX_DELIMIT_COMMA,              // 2C , - expression barrier
    LEX_DELIMIT_LEFT_PAREN,         // 28 (
    LEX_DELIMIT_RIGHT_PAREN,        // 29 )
    LEX_DELIMIT_LEFT_BRACKET,       // 5B [
    LEX_DELIMIT_RIGHT_BRACKET,      // 5D ]

    LEX_DELIMIT_MAX_HARD = LEX_DELIMIT_RIGHT_BRACKET,
    //
    // ^-- As a step toward "Plan -4", the above delimiters are considered to
    // always terminate, e.g. a URL `http://example.com/a)` will not pick up
    // the parenthesis as part of the URL.  But the below delimiters will be
    // picked up, so that `http://example.com/{a} is valid:
    //
    // https://github.com/metaeducation/ren-c/issues/1046

    LEX_DELIMIT_LEFT_BRACE,         // 7B {
    LEX_DELIMIT_RIGHT_BRACE,        // 7D }
    LEX_DELIMIT_DOUBLE_QUOTE,       // 22 "
    LEX_DELIMIT_SLASH,              // 2F / - date, path, file
    LEX_DELIMIT_COLON,              // 3A : - chain (get, set), time
    LEX_DELIMIT_PERIOD,             // 2E . - decimal, tuple, file
    /*LEX_DELIMIT_TILDE,              // 7E ~ - used only by quasiforms */

    LEX_DELIMIT_MAX
};
typedef enum LexDelimitEnum LexDelimit;

STATIC_ASSERT(LEX_DELIMIT_MAX <= 16);

#define Get_Lex_Delimit(b) \
    u_cast(LexDelimit, Get_Lex_Value(b))


/*
**  General Lexical Classes (encoded in the LEX_CLASS field)
**  NOTE: macros do make assumptions on the order, and that there are 4!
*/
enum LexClassEnum {
    LEX_CLASS_DELIMIT = 0,
    LEX_CLASS_SPECIAL,
    LEX_CLASS_WORD,
    LEX_CLASS_NUMBER
};
typedef enum LexClassEnum LexClass;

#define LEX_DELIMIT     (LEX_CLASS_DELIMIT<<LEX_SHIFT)
#define LEX_SPECIAL     (LEX_CLASS_SPECIAL<<LEX_SHIFT)
#define LEX_WORD        (LEX_CLASS_WORD<<LEX_SHIFT)
#define LEX_NUMBER      (LEX_CLASS_NUMBER<<LEX_SHIFT)

#define LEX_FLAG(n)             (1 << (n))
#define Set_Lex_Flag(f,l)       (f = f | LEX_FLAG(l))
#define Has_Lex_Flags(f,l)      (f & (l))
#define Has_Lex_Flag(f,l)       (f & LEX_FLAG(l))
#define Only_Lex_Flag(f,l)      (f == LEX_FLAG(l))

#define Mask_Lex_Class(c)               (g_lex_map[(Byte)c] & LEX_CLASS)
#define Is_Lex_Space(c)                 (!g_lex_map[(Byte)c])
#define Is_Lex_Whitespace(c)            (g_lex_map[(Byte)c]<=LEX_DELIMIT_RETURN)
#define Is_Lex_Delimit(c)               (Mask_Lex_Class(c) == LEX_DELIMIT)
#define Is_Lex_Special(c)               (Mask_Lex_Class(c) == LEX_SPECIAL)
#define Is_Lex_Word(c)                  (Mask_Lex_Class(c) == LEX_WORD)
// Optimization (necessary?)
#define Is_Lex_Number(c)                (g_lex_map[(Byte)c] >= LEX_NUMBER)

#define Is_Lex_Not_Delimit(c)           (g_lex_map[(Byte)c] >= LEX_SPECIAL)
#define Is_Lex_Word_Or_Number(c)        (g_lex_map[(Byte)c] >= LEX_WORD)

#define Is_Lex_Delimit_Hard(byte) \
    (Get_Lex_Delimit(byte) <= LEX_DELIMIT_MAX_HARD)

//
//  Special Chars (encoded in the LEX_VALUE field)
//
// !!! This used to have "LEX_SPECIAL_TILDE" for "7E ~ - complement number",
// but that was removed at some point and it was made a legal word character.
//
enum LexSpecialEnum {             /* The order is important! */
    LEX_SPECIAL_AT,                 /* 40 @ - email */
    LEX_SPECIAL_PERCENT,            /* 25 % - file name */
    LEX_SPECIAL_BACKSLASH,          /* 5C \  */
    LEX_SPECIAL_APOSTROPHE,         /* 27 ' - literal */
    LEX_SPECIAL_LESSER,             /* 3C < - compare or tag */
    LEX_SPECIAL_GREATER,            /* 3E > - compare or end tag */
    LEX_SPECIAL_PLUS,               /* 2B + - positive number */
    LEX_SPECIAL_MINUS,              /* 2D - - date, negative number */
    LEX_SPECIAL_BLANK,              /* 5F _ - blank */

                                    /** Any of these can follow - or ~ : */
    LEX_SPECIAL_POUND,              /* 23 # - hex number */
    LEX_SPECIAL_DOLLAR,             /* 24 $ - money */
    LEX_SPECIAL_SEMICOLON,          // 3B ; - comment

    // LEX_SPECIAL_WORD is not a LEX_VALUE() of anything in LEX_CLASS_SPECIAL,
    // it is used to set a flag by Prescan_Token().
    //
    // !!! Comment said "for nums"
    //
    LEX_SPECIAL_WORD,

    LEX_SPECIAL_MAX
};
typedef enum LexSpecialEnum LexSpecial;

#define Get_Lex_Special(b) \
    u_cast(LexSpecial, Get_Lex_Value(b))

/*
**  Special Encodings
*/
#define LEX_DEFAULT (LEX_DELIMIT|LEX_DELIMIT_SPACE)     /* control chars = spaces */

// In UTF8 C0, C1, F5, and FF are invalid.  Ostensibly set to default because
// it's not necessary to use a bit for a special designation, since they
// should not occur.
//
// !!! If a bit is free, should it be used for errors in the debug build?
//
#define LEX_UTFE LEX_DEFAULT

/*
**  Characters not allowed in Words
*/
#define LEX_FLAGS_NONWORD_SPECIALS \
    (LEX_FLAG(LEX_SPECIAL_AT) \
        | LEX_FLAG(LEX_SPECIAL_PERCENT) \
        | LEX_FLAG(LEX_SPECIAL_BACKSLASH) \
        | LEX_FLAG(LEX_SPECIAL_POUND) \
        | LEX_FLAG(LEX_SPECIAL_DOLLAR))

enum rebol_esc_codes {
    // Must match Esc_Names[]!
    ESC_LINE,
    ESC_TAB,
    ESC_PAGE,
    ESC_ESCAPE,
    ESC_ESC,
    ESC_BACK,
    ESC_DEL,
    ESC_NULL,
    ESC_MAX
};


/*
**  Scanner State Structure
*/

struct TranscodeStateStruct {
    //
    // If vaptr is nullptr, then it's assumed that the `begin` is the source of
    // the UTF-8 data to scan.  Otherwise, it is a variadic feed of UTF-8
    // strings and values that are spliced in.
    //
    va_list *vaptr;

    const Byte *begin;
    const Byte *end;

    // The "limit" feature was not implemented, scanning stopped on a null
    // terminator.  It may be interesting in the future, but it doesn't mix
    // well with scanning variadics which merge Cell and UTF-8 strings
    // together...
    //
    /* const Byte *limit; */

    REBLEN line;
    const Byte *line_head; // head of current line (used for errors)

    Option(String*) file;

    // If the binder isn't nullptr, then any words or arrays are bound into it
    // during the loading process.
    //
    struct Reb_Binder *binder;
    VarList* lib; // does not expand, has negative indices in binder
    VarList* context; // expands, has positive indices in binder
};

typedef TranscodeStateStruct TranscodeState;


struct ScanStateStruct {
    TranscodeState* ss;

    StackIndex stack_base;

    Flags opts;

    // The mode can be '\0', ']', ')', or '/'
    //
    Byte mode;

    REBLEN start_line;
    const Byte *start_line_head;

    // VALUE_FLAG_LINE appearing on a value means that there is a line break
    // *before* that value.  Hence when a newline is seen, it means the *next*
    // value to be scanned will receive the flag.
    //
    bool newline_pending;

    // Number of quotes pending (this old system supports 1, on a few types)
    //
    REBLEN quotes_pending;

    // If we see an "out of turn" : in the scan, we remember that we did so
    // we can produce a GET-WORD! or GET-PATH!.
    //
    bool sigil_pending;
};

typedef struct ScanStateStruct ScanState;


#define ANY_CR_LF_END(c) ((c) == '\0' or (c) == CR or (c) == LF)

#define SCAN_MASK_NONE 0

enum {
    SCAN_FLAG_NEXT = 1 << 0, // load/next feature
    SCAN_FLAG_1 = 1 << 1, // no error throw
    SCAN_FLAG_NULLEDS_LEGAL = 1 << 2, // NULL splice in top level of rebValue()
    SCAN_FLAG_LOCK_SCANNED = 1 << 3  // lock series as they are loaded
};


//
// MAXIMUM LENGTHS
//
// These are the maximum input lengths in bytes needed for a buffer to give
// to Scan_XXX (not including terminator?)  The TO conversions from strings
// tended to hardcode the numbers, so that hardcoding is excised here to
// make it more clear what those numbers are and what their motivation might
// have been (not all were explained).
//
// (See also MAX_HEX_LEN, MAX_INT_LEN)
//

// 30-September-10000/12:34:56.123456789AM/12:34
#define MAX_SCAN_DATE 45

// The maximum length a tuple can be in characters legally for Scan_Tuple
// (should be in a better location, but just excised it for clarity.
#define MAX_SCAN_TUPLE (11 * 4 + 1)

#define MAX_SCAN_DECIMAL 24

#define MAX_SCAN_MONEY 36

#define MAX_SCAN_TIME 30

#define MAX_SCAN_WORD 255


/*
**  Externally Accessed Variables
*/
extern const Byte g_lex_map[256];


//=////////////////////////////////////////////////////////////////////////=//
//
// Ucs2(*) or Ucs2(const*)- UTF-8 EVERYWHERE UNICODE HELPER MACROS
//
//=////////////////////////////////////////////////////////////////////////=//
//
// R3-Alpha historically expected constant character widths in strings, of
// either 1 or 2 bytes per character.  This idea of varying the storage widths
// was replaced in modern Ren-C by embracing the concept of "UTF-8 Everywhere":
//
// http://utf8everywhere.org
//
// This bootstrap build snapshot was captured at a transitional moment when
// UTF-8 Everywhere was just getting started, and so helper classes were
// developed to avoid naive traversals.
//
// So for instance: instead of simply saying:
//
//     REBUNI *ptr = String_Head(string_series);
//     REBUNI c = *ptr++;
//
// ...the idea is you would write:
//
//     Ucs2(*) ptr = CHR_HEAD(string_series);
//     ptr = Ucs2_Next(&c, ptr); // ++ptr or ptr[n] will error in C++ build
//
// There was significantly more work after this point to get UTF-8 Everywhere
// going, and it will never be patched into this bootstrap build.  So this
// really is just all ripped down to being a synonym for REBUNI, a UCS-2
// character codepoint.  See the main branch for the much more interesting
// and useful final product this was aiming at.
//
// The Ucs2(*) syntax is kept just to help porting any small bits of code
// that mention Utf8(*) backwards into the bootstrap executable, should that
// ever need to happen.

#define Ucs2(x) REBWCHAR x

#define Codepoint_At(p) \
    (*p)

INLINE Ucs2(*) Ucs2_Back(
    REBWCHAR *codepoint_out,
    Ucs2(const*) p
){
    if (codepoint_out != nullptr)
        *codepoint_out = *p;
    return m_cast(Ucs2(*), p - 1); // don't write if input was const!
}

INLINE Ucs2(*) Ucs2_Next(
    REBWCHAR *codepoint_out,
    Ucs2(const*) p
){
    if (codepoint_out != nullptr)
        *codepoint_out = *p;
    return m_cast(Ucs2(*), p + 1);
}

INLINE Ucs2(*) Write_Codepoint(Ucs2(*) p, REBWCHAR codepoint) {
    *p = codepoint;
    return p + 1;
}

#ifdef ITOA64 // Integer to ascii conversion
    #define INT_TO_STR(n,s) \
        _i64toa(n, s_cast(s), 10)
#else
    #define INT_TO_STR(n,s) \
        Form_Int_Len(s, n, MAX_INT_LEN)
#endif

#ifdef ATOI64 // Ascii to integer conversion
    #define CHR_TO_INT(s) \
        _atoi64(cs_cast(s))
#else
    #define CHR_TO_INT(s) \
        strtoll(cs_cast(s), 0, 10)
#endif
