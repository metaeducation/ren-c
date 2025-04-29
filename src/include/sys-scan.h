//
//  file: %sys-scan.h
//  summary: "Lexical Scanner Definitions"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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


//=//// SIGIL ORDER ///////////////////////////////////////////////////////=//
//
// This order needs to match the ordering of the corresponding types for
// within each category that carry sigils.
//
// Also, there is a silly optimization that the scanner uses the same values
// for sits tokens as the sigils.

enum SigilEnum {
    SIGIL_0 = 0,
    SIGIL_META = 1,     // ^
    SIGIL_WILD = 2,     // &
    SIGIL_THE = 3,      // @
    SIGIL_VAR = 4,      // $
    MAX_SIGIL = SIGIL_VAR
};
typedef enum SigilEnum Sigil;


//
//  Tokens returned by the scanner.  Keep in sync with g_token_names[].
//
// There is an optimization in place where the tokens for sigils align with
// the sigil value, making it easy to get a sigil from a token.
//
enum TokenEnum {
    TOKEN_0 = 0,
    TOKEN_CARET = 1,  // SIGIL_META
    TOKEN_AMPERSAND = 2,  // SIGIL_WILD
    TOKEN_AT = 3,  // SIGIL_THE
    TOKEN_DOLLAR = 4,  // SIGIL_VAR
    TOKEN_NEWLINE,
    TOKEN_BLANK,
    TOKEN_COMMA,
    TOKEN_WORD,
    TOKEN_LOGIC,
    TOKEN_INTEGER,
    TOKEN_DECIMAL,
    TOKEN_PERCENT,
    TOKEN_GROUP_END,
    TOKEN_GROUP_BEGIN,
    TOKEN_BLOCK_END,
    TOKEN_BLOCK_BEGIN,
    TOKEN_FENCE_END,
    TOKEN_FENCE_BEGIN,
    TOKEN_MONEY,
    TOKEN_TIME,
    TOKEN_DATE,
    TOKEN_CHAR,
    TOKEN_APOSTROPHE,
    TOKEN_TILDE,
    TOKEN_STRING,
    TOKEN_BINARY,
    TOKEN_PAIR,
    TOKEN_TUPLE,  // only triggered in leading dot cases (. .. .foo .foo.bar)
    TOKEN_CHAIN,
    TOKEN_FILE,
    TOKEN_EMAIL,
    TOKEN_URL,
    TOKEN_ISSUE,
    TOKEN_TAG,
    TOKEN_PATH,  // only triggered in leading slash cases (/ // /foo /foo.bar)
    TOKEN_CONSTRUCT,
    TOKEN_END,
    MAX_TOKEN = TOKEN_END
};
typedef enum TokenEnum Token;

STATIC_ASSERT(TOKEN_CARET == cast(int, SIGIL_META));
STATIC_ASSERT(TOKEN_AMPERSAND == cast(int, SIGIL_WILD));
STATIC_ASSERT(TOKEN_AT == cast(int, SIGIL_THE));
STATIC_ASSERT(TOKEN_DOLLAR == cast(int, SIGIL_VAR));



//=//// "LEX MAP" /////////////////////////////////////////////////////////=//
//
// There's a table that encodes a byte's worth of properties for each
// character.  It divides them into 4 "Lex Classes", and then each class can
// encode an additional value.  For example: the LEX_NUMBER class uses the
// bits in the byte that aren't the class to encode the value of the digit.
//
// 1. Macros do make assumptions on the order, and it's important that this
//    fits in two bits
//
// 2. The masks are named like LEX_DELIMIT instead of LEX_DELIMIT_MASK for
//    brevity in the table.  But it's easy to slip up and write something
//    like (Get_Lex_Class(b) == LEX_DELIMIT) instead of LEX_CLASS_DELIMIT.
//    So wrapping in an enum catches that with a -Wenum-compare warning.
//
// 3. The g_lex_map[] only has byte range, so it's important that it's only
//    called on bytes.  But that's not really enforceable in C, you need a
//    C++ template function.  We don't burden the checked build with this as
//    yet-another-runtime cost when inlines aren't optimized out...instead
//    just do it in release/optimized builds.

extern const Byte g_lex_map[256];  // declared in %l-scan.c

typedef Byte Lex;

#define LEX_SHIFT   5                   // shift for encoding classes
#define LEX_CLASS   (3 << LEX_SHIFT)    // class bit field
#define LEX_VALUE   (0x1F)              // value bit field

enum LexClassEnum {  // encoded in LEX_CLASS field, order is important [1]
    LEX_CLASS_DELIMIT = 0,
    LEX_CLASS_SPECIAL,
    LEX_CLASS_WORD,
    LEX_CLASS_NUMBER
};
STATIC_ASSERT(LEX_CLASS_NUMBER < 4);
typedef enum LexClassEnum LexClass;

#define Get_Lex_Class(b) \
    u_cast(LexClass, Lex_Of(b) >> LEX_SHIFT)

enum LexClassMasksEnum {  // using an enum helps catch incorrect uses [2]
    LEX_DELIMIT =   (LEX_CLASS_DELIMIT << LEX_SHIFT),
    LEX_SPECIAL =   (LEX_CLASS_SPECIAL << LEX_SHIFT),
    LEX_WORD =      (LEX_CLASS_WORD << LEX_SHIFT),
    LEX_NUMBER =    (LEX_CLASS_NUMBER << LEX_SHIFT)
};

#if RUNTIME_CHECKS || NO_CPLUSPLUS_11  // ensure Byte when NO_RUNTIME_CHECKS!
    #define Lex_Of(b) \
        g_lex_map[b]

    INLINE Byte Get_Lex_Value(LexClass lexclass, Byte b) {
        assert(Get_Lex_Class(b) == lexclass);
        UNUSED(lexclass);
        return g_lex_map[b] & LEX_VALUE;
    }
#else  // ensure Byte when optimizing, it's "free" [3]
    template<typename B>
    INLINE Lex Lex_Of(B b) {
        static_assert(std::is_same<B,Byte>::value, "Lex_Of() not Byte");
        return g_lex_map[b];
    }

    template<typename B>
    INLINE Byte Get_Lex_Value(LexClass lexclass, B b) {
        static_assert(std::is_same<B,Byte>::value, "Get_Lex_Value() not Byte");
        UNUSED(lexclass);  // we assert it's right in RUNTIME_CHECKS builds
        return g_lex_map[b] & LEX_VALUE;
    }
#endif


//
// Delimiting Chars (encoded in the LEX_VALUE field)
//
enum LexDelimitEnum {
    LEX_DELIMIT_SPACE,              // 20 space
    LEX_DELIMIT_END,                // 00 null terminator, end of input
    LEX_DELIMIT_LINEFEED,           // 0A line-feed
    LEX_DELIMIT_RETURN,             // 0D return

    MAX_LEX_DELIMIT_WHITESPACE = LEX_DELIMIT_RETURN,

    LEX_DELIMIT_COMMA,              // 2C , - expression barrier
    LEX_DELIMIT_LEFT_PAREN,         // 28 (
    LEX_DELIMIT_RIGHT_PAREN,        // 29 )
    LEX_DELIMIT_LEFT_BRACKET,       // 5B [
    LEX_DELIMIT_RIGHT_BRACKET,      // 5D ]

    MAX_LEX_DELIMIT_HARD = LEX_DELIMIT_RIGHT_BRACKET,
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
    LEX_DELIMIT_TILDE,              // 7E ~ - used only by quasiforms

    MAX_LEX_DELIMIT = LEX_DELIMIT_TILDE
};
STATIC_ASSERT(MAX_LEX_DELIMIT < 16);
typedef enum LexDelimitEnum LexDelimit;

#define Get_Lex_Delimit(b) \
    u_cast(LexDelimit, Get_Lex_Value(LEX_CLASS_DELIMIT, (b)))


typedef uint16_t LexFlags;  // 16 flags per lex class

#define LEX_FLAG(n)             (1 << (n))
#define Set_Lex_Flag(f,l)       (f = f | LEX_FLAG(l))
#define Has_Lex_Flags(f,l)      (f & (l))
#define Has_Lex_Flag(f,l)       (f & LEX_FLAG(l))
#define Only_Lex_Flag(f,l)      (f == LEX_FLAG(l))

#define Mask_Lex_Class(b)       (Lex_Of(b) & LEX_CLASS)

#define Is_Lex_Delimit(b)       (Mask_Lex_Class(b) == LEX_DELIMIT)
#define Is_Lex_Special(b)       (Mask_Lex_Class(b) == LEX_SPECIAL)
#define Is_Lex_Word(b)          (Mask_Lex_Class(b) == LEX_WORD)
#define Is_Lex_Number(b)        (Lex_Of(b) >= LEX_NUMBER)

STATIC_ASSERT(LEX_DELIMIT == 0 and LEX_DELIMIT_SPACE == 0);

#define Is_Lex_Space(b) \
    (0 == Lex_Of(b))  // requires LEX_DELIMIT == 0 and LEX_DELIMIT_SPACE == 0

#define Is_Lex_Whitespace(b) \
    (Lex_Of(b) <= MAX_LEX_DELIMIT_WHITESPACE)  // requires LEX_DELIMIT == 0

#define Is_Lex_Not_Delimit(b)           (Lex_Of(b) >= LEX_SPECIAL)
#define Is_Lex_Word_Or_Number(b)        (Lex_Of(b) >= LEX_WORD)

#define Is_Lex_Delimit_Hard(byte) \
    (Get_Lex_Delimit(byte) <= MAX_LEX_DELIMIT_HARD)

//
//  Special Chars (encoded in the LEX_VALUE field)
//
enum LexSpecialEnum {               // The order is important!

    LEX_SPECIAL_AT,                 // 40 @ - email
    LEX_SPECIAL_PERCENT,            // 25 % - file name
    LEX_SPECIAL_BACKSLASH,          // 5C \ - not used at present
    LEX_SPECIAL_APOSTROPHE,         // 27 ' - quoted
    LEX_SPECIAL_LESSER,             // 3C < - compare or tag
    LEX_SPECIAL_GREATER,            // 3E > - compare or end tag
    LEX_SPECIAL_PLUS,               // 2B + - positive number
    LEX_SPECIAL_MINUS,              // 2D - - date, negative number
    LEX_SPECIAL_BAR,                // 7C | - can be part of an "arrow word"
    LEX_SPECIAL_UNDERSCORE,         // 5F _ - blank

                                    // Any of these can follow - or ~ :

    LEX_SPECIAL_POUND,              // 23 # - hex number
    LEX_SPECIAL_DOLLAR,             // 24 $ - money
    LEX_SPECIAL_SEMICOLON,          // 3B ; - comment

    // LEX_SPECIAL_WORD is not a LEX_VALUE() of anything in LEX_CLASS_SPECIAL,
    // it is used to set a flag by Prescan_Token().
    //
    LEX_SPECIAL_WORD,

    LEX_SPECIAL_UTF8_ERROR,  // !!! This wasn't actually used e.g. by UTFE

    MAX_LEX_SPECIAL = LEX_SPECIAL_UTF8_ERROR
};
STATIC_ASSERT(MAX_LEX_SPECIAL < 16);
typedef enum LexSpecialEnum LexSpecial;

#define Get_Lex_Special(b) \
    u_cast(LexSpecial, Get_Lex_Value(LEX_CLASS_SPECIAL, (b)))


#define Get_Lex_Number(b) \
    Get_Lex_Value(LEX_CLASS_NUMBER, (b))


#define LEX_DEFAULT (LEX_DELIMIT|LEX_DELIMIT_SPACE)  // control chars = spaces

// In UTF8 C0, C1, F5, and FF are invalid.  Ostensibly set to default because
// it's not necessary to use a bit for a special designation, since they
// should not occur.
//
// !!! If a bit is free, should it be used for errors in the checked build?
//
#define LEX_UTFE LEX_DEFAULT

//
// Characters not allowed in Words
//
#define LEX_FLAGS_NONWORD_SPECIALS \
    (LEX_FLAG(LEX_SPECIAL_AT) \
        | LEX_FLAG(LEX_SPECIAL_PERCENT) \
        | LEX_FLAG(LEX_SPECIAL_BACKSLASH) \
        | LEX_FLAG(LEX_SPECIAL_POUND) \
        | LEX_FLAG(LEX_SPECIAL_DOLLAR) \
        | LEX_FLAG(LEX_SPECIAL_SEMICOLON))


// If class LEX_WORD or LEX_NUMBER, there is a value contained in the mask
// which is the value of that "digit".  So A-F and a-f can quickly get their
// numeric values, alongside 0-9 getting its numeric value.
//
// Note, this function relies on LEX_WORD lex values having a LEX_VALUE
// field of zero, except for hex values.
//
INLINE bool Try_Get_Lex_Hexdigit_Helper(Sink(Byte) nibble, Lex lex) {
    if (not (lex >= LEX_WORD))  // inlining of Is_Lex_Word_Or_Number()
        return false;
    Byte value = lex & LEX_VALUE;
    if (lex < LEX_NUMBER and value == 0)  // not A-F or a-f
        return false;
    *nibble = value;
    return true;
}

#define Try_Get_Lex_Hexdigit(nibble,b) \
    Try_Get_Lex_Hexdigit_Helper((nibble), Lex_Of(b))  // make sure it's a Byte


// The Lex table was used to speed up ENHEX with this switch() code.  But it
// would break if the Lex values were adjusted.  This isolates it into a
// function that the debug build tests for all characters against the spec
// at startup, to make it more rigorous.
//
INLINE bool Ascii_Char_Needs_Percent_Encoding(Byte b) {
    assert(b != '\0');  // don't call on NUL character
    assert(b < 0x80);  // help avoid accidental calls on partial UTF-8
    switch (Get_Lex_Class(b)) {
      case LEX_CLASS_DELIMIT:
        switch (Get_Lex_Delimit(b)) {
          case LEX_DELIMIT_SPACE:  // includes control characters
          case LEX_DELIMIT_END:  // 00 null terminator
          case LEX_DELIMIT_LINEFEED:
          case LEX_DELIMIT_RETURN:  // e.g. ^M
          case LEX_DELIMIT_LEFT_BRACE:
          case LEX_DELIMIT_RIGHT_BRACE:
          case LEX_DELIMIT_DOUBLE_QUOTE:
            return true;

          default:
            return false;
        }

      case LEX_CLASS_SPECIAL:
        switch (Get_Lex_Special(b)) {
            case LEX_SPECIAL_AT:
            case LEX_SPECIAL_APOSTROPHE:
            case LEX_SPECIAL_PLUS:
            case LEX_SPECIAL_MINUS:
            case LEX_SPECIAL_UNDERSCORE:
            case LEX_SPECIAL_POUND:
            case LEX_SPECIAL_DOLLAR:
            case LEX_SPECIAL_SEMICOLON:
              return false;

            case LEX_SPECIAL_WORD:
              assert(false);  // only occurs in use w/Prescan_Token()
              return false;

            case LEX_SPECIAL_UTF8_ERROR:  // not for c < 0x80
              assert(false);
              return true;

            default:
              return true;
        }

      case LEX_CLASS_WORD:
        if (
            (b >= 'a' and b <= 'z') or (b >= 'A' and b <= 'Z')
            or b == '?' or b == '!' or b == '&'
            or b == '*' or b == '='
        ){
            return false;
        }
        return true;

      case LEX_CLASS_NUMBER:  // 0-9 needs no encoding.
        return false;

      default:
        assert(false);  // gcc doesn't think the above is exhaustive (it is)
        return true;
    }
}


enum EscapeCodeEnum {  // Must match g_escape_info[]!
    ESC_LINE,
    ESC_TAB,
    ESC_PAGE,
    ESC_ESCAPE,
    ESC_ESC,
    ESC_BACK,
    ESC_DEL,
    ESC_NULL,
    MAX_ESC = ESC_NULL
};

typedef struct {
    unsigned char byte;
    const char* name;
} EscapeInfo;

extern const EscapeInfo g_escape_info[MAX_ESC + 1];


#define ANY_CR_LF_END(c) ((c) == '\0' or (c) == CR or (c) == LF)


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
INLINE Option(const Byte*) Skip_To_Byte(
    const Byte* cp,
    const Byte* ep,
    Byte b
){
    while (cp != ep and *cp != b)
        ++cp;
    if (*cp == b)
        return cp;
    return nullptr;
}
