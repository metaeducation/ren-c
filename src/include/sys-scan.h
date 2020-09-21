//
//  File: %sys-scan.h
//  Summary: "Lexical Scanner Definitions"
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

extern const REBYTE Lex_Map[256];  // declared in %l-scan.c

//
//  Tokens returned by the scanner.  Keep in sync with Token_Names[].
//
// !!! There was a micro-optimization in R3-Alpha which made the order of
// tokens align with types, e.g. TOKEN_WORD + 1 => TOKEN_GET_WORD, and
// REB_WORD + 1 => SET_WORD.  As optimizations go, it causes annoyances when
// the type table is rearranged.  A better idea might be to use REB_XXX
// values as the tokens themselves--the main reason not to do this seems to
// be because of the Token_Names[] array.
//
enum Reb_Token {
    TOKEN_END = 0,
    TOKEN_NEWLINE,
    TOKEN_BLANK,
    TOKEN_GET,
    TOKEN_SET,
    TOKEN_SYM,
    TOKEN_WORD,
    TOKEN_LOGIC,
    TOKEN_INTEGER,
    TOKEN_DECIMAL,
    TOKEN_PERCENT,
    TOKEN_GET_GROUP_BEGIN,
    TOKEN_SYM_GROUP_BEGIN,
    TOKEN_GROUP_END,
    TOKEN_GROUP_BEGIN,
    TOKEN_GET_BLOCK_BEGIN,
    TOKEN_SYM_BLOCK_BEGIN,
    TOKEN_BLOCK_END,
    TOKEN_BLOCK_BEGIN,
    TOKEN_MONEY,
    TOKEN_TIME,
    TOKEN_DATE,
    TOKEN_CHAR,
    TOKEN_APOSTROPHE,
    TOKEN_STRING,
    TOKEN_BINARY,
    TOKEN_PAIR,
    TOKEN_TUPLE,  // only triggered in leading dot cases (. .. .foo .foo.bar)
    TOKEN_FILE,
    TOKEN_EMAIL,
    TOKEN_URL,
    TOKEN_ISSUE,
    TOKEN_TAG,
    TOKEN_PATH,  // only triggered in leading slash cases (/ // /foo /foo.bar)
    TOKEN_CONSTRUCT,
    TOKEN_MAX
};

inline static enum Reb_Kind KIND_OF_WORD_FROM_TOKEN(enum Reb_Token t) {

    // !!! Temporarily disable optimization due to type table rearrangement

    if (t == TOKEN_WORD)
        return REB_WORD;
    if (t == TOKEN_SET)
        return REB_SET_WORD;
    if (t == TOKEN_GET)
        return REB_GET_WORD;
    if (t == TOKEN_SYM)
        return REB_SYM_WORD;
    assert(!"Bad token passed to KIND_OF_WORD_FROM_TOKEN()");
    return REB_0_END;
}

inline static enum Reb_Kind KIND_OF_ARRAY_FROM_TOKEN(enum Reb_Token t) {

    // !!! Temporarily disable optimization due to type table rearrangement

    if (t == TOKEN_GROUP_BEGIN)
        return REB_GROUP;
    if (t == TOKEN_BLOCK_BEGIN)
        return REB_BLOCK;
    if (t == TOKEN_GET_GROUP_BEGIN)
        return REB_GET_GROUP;
    if (t == TOKEN_GET_BLOCK_BEGIN)
        return REB_GET_BLOCK;
    if (t == TOKEN_SYM_GROUP_BEGIN)
        return REB_SYM_GROUP;
    if (t == TOKEN_SYM_BLOCK_BEGIN)
        return REB_SYM_BLOCK;
    assert(!"Bad token passed to KIND_OF_ARRAY_FROM_TOKEN()");
    return REB_0_END;
}


/*
**  Lexical Table Entry Encoding
*/
#define LEX_SHIFT       5               /* shift for encoding classes */
#define LEX_CLASS       (3<<LEX_SHIFT)  /* class bit field */
#define LEX_VALUE       (0x1F)          /* value bit field */

#define GET_LEX_CLASS(c)  (Lex_Map[(REBYTE)c] >> LEX_SHIFT)
#define GET_LEX_VALUE(c)  (Lex_Map[(REBYTE)c] & LEX_VALUE)


/*
**  Delimiting Chars (encoded in the LEX_VALUE field)
**  NOTE: Macros do make assumption that _RETURN is the last space delimiter
*/
enum LEX_DELIMIT_ENUM {
    LEX_DELIMIT_SPACE,              /* 20 space */
    LEX_DELIMIT_END,                /* 00 null terminator, end of input */
    LEX_DELIMIT_LINEFEED,           /* 0A line-feed */
    LEX_DELIMIT_RETURN,             /* 0D return */
    LEX_DELIMIT_LEFT_PAREN,         /* 28 ( */
    LEX_DELIMIT_RIGHT_PAREN,        /* 29 ) */
    LEX_DELIMIT_LEFT_BRACKET,       /* 5B [ */
    LEX_DELIMIT_RIGHT_BRACKET,      /* 5D ] */
    LEX_DELIMIT_SEMICOLON,          /* 3B ; */

    // As a step toward "Plan -4", the above delimiters are considered to
    // always terminate, e.g. a URL `http://example.com/a)` will not pick up
    // the parenthesis as part of the URL.  But the below delimiters will be
    // picked up, so that `http://example.com/{a} is valid:
    //
    // https://github.com/metaeducation/ren-c/issues/1046
    //
    // Note: If you rearrange these, update IS_LEX_DELIMIT_HARD !

    LEX_DELIMIT_LEFT_BRACE,         /* 7B } */
    LEX_DELIMIT_RIGHT_BRACE,        /* 7D } */
    LEX_DELIMIT_DOUBLE_QUOTE,       /* 22 " */
    LEX_DELIMIT_SLASH,              /* 2F / - date, path, file */

    LEX_DELIMIT_UTF8_ERROR,

    LEX_DELIMIT_MAX
};


/*
**  General Lexical Classes (encoded in the LEX_CLASS field)
**  NOTE: macros do make assumptions on the order, and that there are 4!
*/
enum LEX_CLASS_ENUM {
    LEX_CLASS_DELIMIT = 0,
    LEX_CLASS_SPECIAL,
    LEX_CLASS_WORD,
    LEX_CLASS_NUMBER
};

#define LEX_DELIMIT     (LEX_CLASS_DELIMIT<<LEX_SHIFT)
#define LEX_SPECIAL     (LEX_CLASS_SPECIAL<<LEX_SHIFT)
#define LEX_WORD        (LEX_CLASS_WORD<<LEX_SHIFT)
#define LEX_NUMBER      (LEX_CLASS_NUMBER<<LEX_SHIFT)

typedef uint16_t LEXFLAGS;  // 16 flags per lex class

#define LEX_FLAG(n)             (1 << (n))
#define SET_LEX_FLAG(f,l)       (f = f | LEX_FLAG(l))
#define HAS_LEX_FLAGS(f,l)      (f & (l))
#define HAS_LEX_FLAG(f,l)       (f & LEX_FLAG(l))
#define ONLY_LEX_FLAG(f,l)      (f == LEX_FLAG(l))

#define MASK_LEX_CLASS(c)               (Lex_Map[(REBYTE)c] & LEX_CLASS)
#define IS_LEX_SPACE(c)                 (!Lex_Map[(REBYTE)c])
#define IS_LEX_ANY_SPACE(c)             (Lex_Map[(REBYTE)c]<=LEX_DELIMIT_RETURN)
#define IS_LEX_DELIMIT(c)               (MASK_LEX_CLASS(c) == LEX_DELIMIT)
#define IS_LEX_SPECIAL(c)               (MASK_LEX_CLASS(c) == LEX_SPECIAL)
#define IS_LEX_WORD(c)                  (MASK_LEX_CLASS(c) == LEX_WORD)
// Optimization (necessary?)
#define IS_LEX_NUMBER(c)                (Lex_Map[(REBYTE)c] >= LEX_NUMBER)

#define IS_LEX_NOT_DELIMIT(c)           (Lex_Map[(REBYTE)c] >= LEX_SPECIAL)
#define IS_LEX_WORD_OR_NUMBER(c)        (Lex_Map[(REBYTE)c] >= LEX_WORD)

inline static bool IS_LEX_DELIMIT_HARD(REBYTE c) {
    assert(IS_LEX_DELIMIT(c));
    return GET_LEX_VALUE(c) <= LEX_DELIMIT_RIGHT_BRACKET;
}

//
//  Special Chars (encoded in the LEX_VALUE field)
//
// !!! This used to have "LEX_SPECIAL_TILDE" for "7E ~ - complement number",
// but that was removed at some point and it was made a legal word character.
//
enum LEX_SPECIAL_ENUM {             /* The order is important! */
    LEX_SPECIAL_AT,                 /* 40 @ - email */
    LEX_SPECIAL_PERCENT,            /* 25 % - file name */
    LEX_SPECIAL_BACKSLASH,          /* 5C \  */
    LEX_SPECIAL_COLON,              /* 3A : - time, get, set */
    LEX_SPECIAL_APOSTROPHE,         /* 27 ' - literal */
    LEX_SPECIAL_LESSER,             /* 3C < - compare or tag */
    LEX_SPECIAL_GREATER,            /* 3E > - compare or end tag */
    LEX_SPECIAL_PLUS,               /* 2B + - positive number */
    LEX_SPECIAL_MINUS,              /* 2D - - date, negative number */
    LEX_SPECIAL_BAR,                /* 7C | - expression barrier */
    LEX_SPECIAL_BLANK,              /* 5F _ - blank */

                                    /** Any of these can follow - or ~ : */
    LEX_SPECIAL_PERIOD,             /* 2E . - decimal number */
    LEX_SPECIAL_COMMA,              /* 2C , - decimal number */
    LEX_SPECIAL_POUND,              /* 23 # - hex number */
    LEX_SPECIAL_DOLLAR,             /* 24 $ - money */

    // LEX_SPECIAL_WORD is not a LEX_VALUE() of anything in LEX_CLASS_SPECIAL,
    // it is used to set a flag by Prescan_Token().
    //
    // !!! Comment said "for nums"
    //
    LEX_SPECIAL_WORD,

    LEX_SPECIAL_MAX
};

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
#define LEX_WORD_FLAGS (LEX_FLAG(LEX_SPECIAL_AT) |              \
                        LEX_FLAG(LEX_SPECIAL_PERCENT) |         \
                        LEX_FLAG(LEX_SPECIAL_BACKSLASH) |       \
                        LEX_FLAG(LEX_SPECIAL_COMMA) |           \
                        LEX_FLAG(LEX_SPECIAL_POUND) |           \
                        LEX_FLAG(LEX_SPECIAL_DOLLAR) |          \
                        LEX_FLAG(LEX_SPECIAL_COLON))

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


//=//// SCANNER STATE STRUCTURES //////////////////////////////////////////=//
//
// R3-Alpha had a single state structure called SCAN_STATE, which was passed
// between recursions of the scanner.  Ren-C breaks this into two parts: the
// scanner's current position in the state (current line number, current token
// beginning and end byte pointers)...and properties unique to each level
// (what kind of array is being scanned, where the line was when that array
// started, etc.)
//
// This was introduced to try and improve some error messages that were not
// able to accurately track unique properties across recursion levels.  e.g.
// if a nested block is going to give an error about an unmatched bracket,
// it wants that error to point to the line number of the start of what it
// was trying to match.  But it cannot overwrite the line number of a
// shared scan state without potentially garbling the reported start line
// of the outer recursion.  Separate variables are needed--and it's clearer
// to break them out into structures than to try and use local variables
//

typedef struct rebol_scan_state {  // shared across all levels of a scan
    //
    // Beginning and end positions of currently processed token.
    //
    const REBYTE *begin;
    const REBYTE *end;

    // If feed is NULL, then it is assumed that the `begin` is the source of
    // the UTF-8 data to scan.  Otherwise, it is a variadic feed of UTF-8
    // strings and values that are spliced in.
    //
    struct Reb_Feed *feed;

    REBSTR *file;  // file currently being scanned (or anonymous)

    REBLIN line;  // line number where current scan position is
    const REBYTE *line_head;  // pointer to head of current line (for errors)

    // The "limit" feature was not implemented, scanning just stopped at '\0'.
    // It may be interesting in the future, but it doesn't mix well with
    // scanning variadics which merge REBVAL and UTF-8 strings together...
    //
    /* const REBYTE *limit; */

    // !!! R3-Alpha had a /RELAX mode for TRANSCODE, which offered the ability
    // to get a partial scan with an error on a token.  An error propagating
    // out via fail() would not allow a user to get such partial results
    // (unless they were parameters to the error).  The feature was not really
    // specified well...but without some more recoverable notion of state in a
    // nested parse, only errors at the topmost level can be meaningful.  So
    // Ren-C has this flag which is set by the scanner on failure.  A better
    // notion would likely integrate with PARSE.  In any case, we track the
    // depth so that a failure can potentially be recovered from at 0.
    //
    REBLEN depth;
} SCAN_STATE;

typedef struct rebol_scan_level {  // each array scan corresponds to a level
    SCAN_STATE *ss;  // shared state of where the scanner head currently is

    // '\0' => top level scan
    // ']' => this level is scanning a block
    // ')' => this level is scanning a group
    // '/' => this level is scanning a path
    // '.' => this level is scanning a tuple
    //
    // (Chosen as the terminal character to use in error messages for the
    // character we are seeking to find a match for).
    //
    REBYTE mode;

    REBLEN start_line;
    const REBYTE *start_line_head;

    // CELL_FLAG_LINE appearing on a value means that there is a line break
    // *before* that value.  Hence when a newline is seen, it means the *next*
    // value to be scanned will receive the flag.
    //
    bool newline_pending;

    REBFLGS opts;
} SCAN_LEVEL;

#define ANY_CR_LF_END(c) ((c) == '\0' or (c) == CR or (c) == LF)

enum {
    SCAN_FLAG_NEXT = 1 << 0, // load/next feature
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


// Skip to the specified byte but not past the provided end pointer of bytes.
// nullptr if byte is not found.
//
inline static const REBYTE *Skip_To_Byte(
    const REBYTE *cp,
    const REBYTE *ep,
    REBYTE b
){
    while (cp != ep and *cp != b)
        ++cp;
    if (*cp == b)
        return cp;
    return nullptr;
}
