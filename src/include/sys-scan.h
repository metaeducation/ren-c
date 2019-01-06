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
// There was a micro-optimization in R3-Alpha which made the relative order of
// tokens align with types, e.g. TOKEN_WORD + 1 => TOKEN_GET_WORD, and
// REB_WORD + 1 => SET_WORD.  It's kind of silly, but the order of tokens
// doesn't matter.  So Ren-C made it line up even better, so math is needed
// to transform from tokens to the associated Reb_Kind.
//
// !!! Is there any real reason not to use Reb_Kind and just throw in a few
// pseudotypes for things that are missing?  (e.g. REB_TOK_NEWLINE could be
// a value > REB_MAX).  The main reason not to do this seems to be because
// of the Token_Names[] array, but it seems like it would be simplifying.
//
enum Reb_Token {
    TOKEN_END = 0,
    TOKEN_NEWLINE,
    TOKEN_BLANK,
    TOKEN_GET, // should equal REB_GET_WORD
    TOKEN_SET, // should equal REB_SET_WORD
    TOKEN_WORD, // should equal REB_WORD
    TOKEN_LOGIC, // !!! Currently not used LOGIC!, uses #[true] and #[false]
    TOKEN_INTEGER,
    TOKEN_DECIMAL,
    TOKEN_PERCENT,
    TOKEN_GET_GROUP_BEGIN, // should equal REB_GET_GROUP
    TOKEN_GROUP_END,
    TOKEN_GROUP_BEGIN, // should equal REB_GROUP
    TOKEN_GET_BLOCK_BEGIN, // should equal REB_GET_BLOCK
    TOKEN_BLOCK_END,
    TOKEN_BLOCK_BEGIN, // should equal REB_BLOCK
    TOKEN_MONEY,
    TOKEN_TIME,
    TOKEN_DATE,
    TOKEN_CHAR,
    TOKEN_APOSTROPHE,
    TOKEN_STRING,
    TOKEN_BINARY,
    TOKEN_PAIR,
    TOKEN_TUPLE,
    TOKEN_FILE,
    TOKEN_EMAIL,
    TOKEN_URL,
    TOKEN_ISSUE,
    TOKEN_TAG,
    TOKEN_PATH,
    TOKEN_CONSTRUCT,
    TOKEN_MAX
};

inline static enum Reb_Kind KIND_OF_WORD_FROM_TOKEN(enum Reb_Token t) {
    enum Reb_Kind k = cast(enum Reb_Kind, t);

  #if !defined(NDEBUG)
    if (t == TOKEN_GET)
        assert(k == REB_GET_WORD);
    else if (t == TOKEN_SET)
        assert(k == REB_SET_WORD);
    else if (t == TOKEN_WORD)
        assert(k == REB_WORD);
    else
        assert(!"Bad token passed to KIND_OF_WORD_FROM_TOKEN()");
  #endif

    return k;
}

inline static enum Reb_Kind KIND_OF_ARRAY_FROM_TOKEN(enum Reb_Token t) {
    enum Reb_Kind k = cast(enum Reb_Kind, t);

  #if !defined(NDEBUG)
    if (t == TOKEN_GET_BLOCK_BEGIN)
        assert(k == REB_GET_BLOCK);
    else if (t == TOKEN_BLOCK_BEGIN)
        assert(k == REB_BLOCK);
    else if (t == TOKEN_GET_GROUP_BEGIN)
        assert(k == REB_GET_GROUP);
    else if (t == TOKEN_GROUP_BEGIN)
        assert(k == REB_GROUP);
    else
        assert(!"Bad token passed to KIND_OF_ARRAY_FROM_TOKEN()");
  #endif

    return k;
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
    LEX_DELIMIT_LEFT_BRACE,         /* 7B } */
    LEX_DELIMIT_RIGHT_BRACE,        /* 7D } */
    LEX_DELIMIT_DOUBLE_QUOTE,       /* 22 " */
    LEX_DELIMIT_SLASH,              /* 2F / - date, path, file */
    LEX_DELIMIT_SEMICOLON,          /* 3B ; */
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


/*
**  Scanner State Structure
*/

typedef struct rebol_scan_state {
    //
    // The mode_char can be '\0', ']', ')', or '/'
    //
    REBYTE mode_char;

    // If vaptr is NULL, then it is assumed that the `begin` is the source of
    // the UTF-8 data to scan.  Otherwise, it is a variadic feed of UTF-8
    // strings and values that are spliced in.
    //
    va_list *vaptr;

    const REBYTE *begin;
    const REBYTE *end;

    // The "limit" feature was not implemented, scanning stopped on a null
    // terminator.  It may be interesting in the future, but it doesn't mix
    // well with scanning variadics which merge REBVAL and UTF-8 strings
    // together...
    //
    /* const REBYTE *limit; */
    
    REBCNT line;
    const REBYTE *line_head; // head of current line (used for errors)

    REBCNT start_line;
    const REBYTE *start_line_head;

    REBSTR *file;

    // CELL_FLAG_LINE appearing on a value means that there is a line break
    // *before* that value.  Hence when a newline is seen, it means the *next*
    // value to be scanned will receive the flag.
    //
    bool newline_pending;

    REBFLGS opts;
    enum Reb_Token token;

    // If the binder isn't NULL, then any words or arrays are bound into it
    // during the loading process.
    //
    struct Reb_Binder *binder;
    REBCTX *lib; // does not expand, has negative indices in binder
    REBCTX *context; // expands, has positive indices in binder
} SCAN_STATE;

#define ANY_CR_LF_END(c) ((c) == '\0' or (c) == CR or (c) == LF)

enum {
    SCAN_FLAG_NEXT = 1 << 0, // load/next feature
    SCAN_FLAG_ONLY = 1 << 1, // only single value (no blocks)
    SCAN_FLAG_RELAX = 1 << 2, // no error throw
    SCAN_FLAG_NULLEDS_LEGAL = 1 << 3, // NULL splice in top level of rebRun()
    SCAN_FLAG_LOCK_SCANNED = 1 << 4  // lock series as they are loaded
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
extern const REBYTE Lex_Map[256];


//=////////////////////////////////////////////////////////////////////////=//
//
// REBCHR(*) or REBCHR(const *)- UTF-8 EVERYWHERE UNICODE HELPER MACROS
//
//=////////////////////////////////////////////////////////////////////////=//
//
// R3-Alpha historically expected constant character widths in strings, of
// either 1 or 2 bytes per character.  This idea of varying the storage widths
// is being replaced by embracing the concept of "UTF-8 Everywhere":
//
// http://utf8everywhere.org
//
// In order to assist in retrofitting code under the old expectations, the C++
// build uses a class that disables the ability to directly increment or
// decrement pointers to REBCHR without going through helper routines.  To get
// this checking, raw pointers cannot be used.  So a technique described here
// was used to create the REBCHR(*) macro to be used in place of REBUNI*:
//
// http://blog.hostilefork.com/kinda-smart-pointers-in-c/
//
// So for instance: instead of simply saying:
//
//     REBUNI *ptr = UNI_HEAD(string_series);
//     REBUNI c = *ptr++;
//
// ...one must instead write:
//
//     REBCHR(*) ptr = CHR_HEAD(string_series);
//     ptr = NEXT_CHR(&c, ptr); // ++ptr or ptr[n] will error in C++ build
//
// The code that runs behind the scenes is currently equivalent to the pointer
// incrementing and decrementing.  But it will become typical UTF-8 forward
// and backward scanning code after the conversion.
//

#ifdef CPLUSPLUS_11
    template<class T>
    class RebchrPtr;

    template<>
    class RebchrPtr<const void*> {
    protected:
        REBWCHAR *p;

    public:
        RebchrPtr () {}
        RebchrPtr (const REBWCHAR *p) : p (const_cast<REBWCHAR *>(p)) {}

        RebchrPtr back(REBWCHAR *codepoint_out) {
            if (codepoint_out != NULL)
                *codepoint_out = *p;
            return p - 1;
        }

        RebchrPtr next(REBWCHAR *codepoint_out) {
            if (codepoint_out != NULL)
                *codepoint_out = *p;
            return p + 1;
        }

        REBWCHAR code() {
            REBWCHAR temp;
            next(&temp);
            return temp;
        }

        const REBWCHAR *as_rebuni() {
            return p;
        }

        bool operator==(const RebchrPtr<const void*> &other) {
            return p == other.p;
        }

        bool operator!=(const RebchrPtr<const void*> &other) {
            return !(*this == other);
        }
    };

    template<>
    class RebchrPtr<void *> : public RebchrPtr<const void *> {
    
    public:
        RebchrPtr () : RebchrPtr<const void*>() {}
        RebchrPtr (REBWCHAR *p) : RebchrPtr<const void*> (p) {}

        RebchrPtr back(REBWCHAR *codepoint_out) {
            if (codepoint_out != NULL)
                *codepoint_out = *p;
            return p - 1;
        }

        RebchrPtr next(REBWCHAR *codepoint_out) {
            if (codepoint_out != NULL)
                *codepoint_out = *p;
            return p + 1;
        }

        RebchrPtr write(REBWCHAR codepoint) {
            *p = codepoint;
            return p + 1;
        }

        REBWCHAR *as_rebuni() {
            return p;
        }

        static RebchrPtr<const void *> as_rebchr(const REBWCHAR *p) {
            return p;
        }

        static RebchrPtr<void *> as_rebchr(REBWCHAR *p) {
            return p;
        }
    };

    #define REBCHR(x) RebchrPtr<void x>

    #define CHR_CODE(p) \
        (p).code()

    #define BACK_CHR(codepoint_out, p) \
        (p).back(codepoint_out)

    #define NEXT_CHR(codepoint_out, p) \
        (p).next(codepoint_out)

    #define WRITE_CHR(p, codepoint) \
        (p).write(codepoint)

    #define AS_REBUNI(p) \
        (p).as_rebuni()

    #define AS_REBCHR(p) \
        RebchrPtr<void *>::as_rebchr(p)
#else
    #define REBCHR(x) REBWCHAR x

    #define CHR_CODE(p) \
        (*p)

    inline static REBCHR(*) BACK_CHR(
        REBWCHAR *codepoint_out,
        REBCHR(const *) p
    ){
        if (codepoint_out != NULL)
            *codepoint_out = *p;
        return m_cast(REBCHR(*), p - 1); // don't write if input was const!
    }

    inline static REBCHR(*) NEXT_CHR(
        REBWCHAR *codepoint_out,
        REBCHR(const *) p
    ){
        if (codepoint_out != NULL)
            *codepoint_out = *p;
        return m_cast(REBCHR(*), p + 1);
    }

    inline static REBCHR(*) WRITE_CHR(REBCHR(*) p, REBWCHAR codepoint) {
        *p = codepoint;
        return p + 1;
    }

    #define AS_REBUNI(p) \
        (p)

    #define AS_REBCHR(p) \
        (p)
#endif

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
