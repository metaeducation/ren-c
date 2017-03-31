//
//  File: %s-mold.c
//  Summary: "value to string conversion"
//  Section: strings
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

//#define   INCLUDE_TYPE_NAMES      // include the value names table
#include "sys-core.h"
#include <float.h>

#define MAX_QUOTED_STR  50  // max length of "string" before going to { }

//typedef REBSER *(*MOLD_FUNC)(REBVAL *, REBSER *, REBCNT);
typedef void (*MOLD_FUNC)(REBVAL *, REB_MOLD *);

//const REBYTE New_Line[4] = {LF, 0};

const REBYTE Punctuation[] = ".,-/";
enum REB_Punct {
    PUNCT_DOT = 0, // Must be 0
    PUNCT_COMMA,   // Must be 1
    PUNCT_DASH,
    PUNCT_SLASH,
    PUNCT_MAX
};

REBYTE *Char_Escapes;
#define MAX_ESC_CHAR (0x60-1) // size of escape table
#define IS_CHR_ESC(c) ((c) <= MAX_ESC_CHAR && Char_Escapes[c])

REBYTE *URL_Escapes;
#define MAX_URL_CHAR (0x80-1)
#define IS_URL_ESC(c)  ((c) <= MAX_URL_CHAR && (URL_Escapes[c] & ESC_URL))
#define IS_FILE_ESC(c) ((c) <= MAX_URL_CHAR && (URL_Escapes[c] & ESC_FILE))

enum {
    ESC_URL = 1,
    ESC_FILE = 2,
    ESC_EMAIL = 4
};

/***********************************************************************
************************************************************************
**
**  SECTION: Global Mold Utilities
**
************************************************************************
***********************************************************************/

//
//  Emit: C
//
REBSER *Emit(REB_MOLD *mold, const char *fmt, ...)
{
    va_list va;
    REBYTE ender = 0;
    REBSER *series = mold->series;

    assert(SER_WIDE(series) == 2);

    va_start(va, fmt);

    for (; *fmt; fmt++) {
        switch (*fmt) {
        case 'W': { // Word symbol
            const REBVAL *any_word = va_arg(va, const REBVAL*);
            REBSTR *spelling = VAL_WORD_SPELLING(any_word);
            Append_UTF8_May_Fail(
                series, STR_HEAD(spelling), STR_NUM_BYTES(spelling)
            );
            break;
        }

        case 'V':   // Value
            Mold_Value(mold, va_arg(va, const REBVAL*), TRUE);
            break;
        case 'S':   // String of bytes
            Append_Unencoded(series, va_arg(va, const char *));
            break;
        case 'C':   // Char
            Append_Codepoint_Raw(series, va_arg(va, REBCNT));
            break;
        case 'E': {  // Series (byte or uni)
            REBSER *src = va_arg(va, REBSER*);
            Insert_String(
                series, SER_LEN(series), src, 0, SER_LEN(src), FALSE
            );
            break;
        }
        case 'I':   // Integer
            Append_Int(series, va_arg(va, REBINT));
            break;
        case 'i':
            Append_Int_Pad(series, va_arg(va, REBINT), -9);
            Trim_Tail(mold->series, '0');
            break;
        case '2':   // 2 digit int (for time)
            Append_Int_Pad(series, va_arg(va, REBINT), 2);
            break;
        case 'T': {  // Type name
            const REBYTE *bytes = Get_Type_Name(va_arg(va, REBVAL*));
            Append_UTF8_May_Fail(series, bytes, LEN_BYTES(bytes));
            break; }
        case 'N': {  // Symbol name
            REBSTR *spelling = va_arg(va, REBSTR*);
            Append_UTF8_May_Fail(
                series, STR_HEAD(spelling), STR_NUM_BYTES(spelling)
            );
            break; }
        case '+':   // Add #[ if mold/all
            if (GET_MOPT(mold, MOPT_MOLD_ALL)) {
                Append_Unencoded(series, "#[");
                ender = ']';
            }
            break;
        case 'D':   // Datatype symbol: #[type
            if (ender) {
                REBSTR *canon = Canon(cast(REBSYM, va_arg(va, int)));
                Append_UTF8_May_Fail(
                    series, STR_HEAD(canon), STR_NUM_BYTES(canon)
                );
                Append_Codepoint_Raw(series, ' ');
            }
            else
                va_arg(va, REBCNT); // ignore it
            break;
        default:
            Append_Codepoint_Raw(series, *fmt);
        }
    }
    va_end(va);

    if (ender) Append_Codepoint_Raw(series, ender);

    return series;
}


//
//  Prep_String: C
//
// Helper function for the string related Mold functions below.
// Creates or expands the series and provides the location to
// copy text into.
//
REBSER *Prep_String(REBSER *series, REBYTE **str, REBCNT len)
{
    REBCNT tail;

    if (!series) {
        series = Make_Binary(len);
        SET_SERIES_LEN(series, len);
        *str = BIN_HEAD(series);
    }
    else {
        // This used "STR_AT" (obsolete) but didn't have an explicit case
        // here that it was byte sized.  Check it, because if you have
        // unicode characters this would give the wrong pointer.
        //
        assert(BYTE_SIZE(series));

        tail = SER_LEN(series);
        EXPAND_SERIES_TAIL(series, len);
        *str = BIN_AT(series, tail);
    }
    return series;
}


//
//  Prep_Uni_Series: C
//
REBUNI *Prep_Uni_Series(REB_MOLD *mold, REBCNT len)
{
    REBCNT tail = SER_LEN(mold->series);

    EXPAND_SERIES_TAIL(mold->series, len);

    return UNI_AT(mold->series, tail);
}


/***********************************************************************
************************************************************************
**
**  SECTION: Local MOLD Utilities
**
************************************************************************
***********************************************************************/

//
//  Pre_Mold: C
//
// Emit the initial datatype function, depending on /ALL option
//
void Pre_Mold(const RELVAL *value, REB_MOLD *mold)
{
    Emit(mold, GET_MOPT(mold, MOPT_MOLD_ALL) ? "#[T " : "make T ", value);
}


//
//  End_Mold: C
//
// Finish the mold, depending on /ALL with close block.
//
void End_Mold(REB_MOLD *mold)
{
    if (GET_MOPT(mold, MOPT_MOLD_ALL)) Append_Codepoint_Raw(mold->series, ']');
}


//
//  Post_Mold: C
//
// For series that has an index, add the index for mold/all.
// Add closing block.
//
void Post_Mold(const RELVAL *value, REB_MOLD *mold)
{
    if (VAL_INDEX(value)) {
        Append_Codepoint_Raw(mold->series, ' ');
        Append_Int(mold->series, VAL_INDEX(value)+1);
    }
    if (GET_MOPT(mold, MOPT_MOLD_ALL)) Append_Codepoint_Raw(mold->series, ']');
}


//
//  New_Indented_Line: C
//
// Create a newline with auto-indent on next line if needed.
//
void New_Indented_Line(REB_MOLD *mold)
{
    REBINT n;
    REBUNI *cp = 0;

    // Check output string has content already but no terminator:
    if (SER_LEN(mold->series)) {
        cp = UNI_LAST(mold->series);
        if (*cp == ' ' || *cp == '\t') *cp = '\n';
        else cp = 0;
    }

    // Add terminator:
    if (!cp) Append_Codepoint_Raw(mold->series, '\n');

    // Add proper indentation:
    if (!GET_MOPT(mold, MOPT_INDENT)) {
        for (n = 0; n < mold->indent; n++)
            Append_Unencoded(mold->series, "    ");
    }
}


/***********************************************************************
************************************************************************
**
**  SECTION: Char/String Datatypes
**
************************************************************************
***********************************************************************/

typedef struct REB_Str_Flags {
    REBCNT escape;      // escaped chars
    REBCNT brace_in;    // {
    REBCNT brace_out;   // }
    REBCNT newline;     // lf
    REBCNT quote;       // "
    REBCNT paren;       // (1234)
    REBCNT chr1e;
    REBCNT malign;
} REB_STRF;


static void Sniff_String(REBSER *ser, REBCNT idx, REB_STRF *sf)
{
    // Scan to find out what special chars the string contains?
    REBYTE *bp = SER_DATA_RAW(ser);
    REBUNI *up = cast(REBUNI*, bp);
    REBUNI c;
    REBCNT n;

    for (n = idx; n < SER_LEN(ser); n++) {
        c = BYTE_SIZE(ser) ? cast(REBUNI, bp[n]) : up[n];
        switch (c) {
        case '{':
            sf->brace_in++;
            break;
        case '}':
            sf->brace_out++;
            if (sf->brace_out > sf->brace_in) sf->malign++;
            break;
        case '"':
            sf->quote++;
            break;
        case '\n':
            sf->newline++;
            break;
        default:
            if (c == 0x1e) sf->chr1e += 4; // special case of ^(1e)
            else if (IS_CHR_ESC(c)) sf->escape++;
            else if (c >= 0x1000) sf->paren += 6; // ^(1234)
            else if (c >= 0x100)  sf->paren += 5; // ^(123)
            else if (c >= 0x80)   sf->paren += 4; // ^(12)
        }
    }
    if (sf->brace_in != sf->brace_out) sf->malign++;
}

static REBUNI *Emit_Uni_Char(REBUNI *up, REBUNI chr, REBOOL parened)
{
    if (chr >= 0x7f || chr == 0x1e) {  // non ASCII or ^ must be (00) escaped
        if (parened || chr == 0x1e) { // do not AND with above
            *up++ = '^';
            *up++ = '(';
            up = Form_Uni_Hex(up, chr);
            *up++ = ')';
            return up;
        }
    }
    else if (IS_CHR_ESC(chr)) {
        *up++ = '^';
        *up++ = Char_Escapes[chr];
        return up;
    }

    *up++ = chr;
    return up;
}

static void Mold_Uni_Char(REBSER *dst, REBUNI chr, REBOOL molded, REBOOL parened)
{
    REBCNT tail = SER_LEN(dst);

    if (!molded) {
        EXPAND_SERIES_TAIL(dst, 1);
        *UNI_AT(dst, tail) = chr;
    }
    else {
        EXPAND_SERIES_TAIL(dst, 10); // worst case: #"^(1234)"

        REBUNI *up = UNI_AT(dst, tail);
        *up++ = '#';
        *up++ = '"';
        up = Emit_Uni_Char(up, chr, parened);
        *up++ = '"';

        SET_SERIES_LEN(dst, up - UNI_HEAD(dst));
    }
    TERM_UNI(dst);
}

static void Mold_String_Series(const REBVAL *value, REB_MOLD *mold)
{
    REBCNT len = VAL_LEN_AT(value);
    REBSER *ser = VAL_SERIES(value);
    REBCNT idx = VAL_INDEX(value);
    REBYTE *bp;
    REBUNI *up;
    REBUNI *dp;
    REBOOL unicode = NOT(BYTE_SIZE(ser));
    REBCNT n;
    REBUNI c;

    REB_STRF sf;
    CLEARS(&sf);

    // Empty string:
    if (idx >= VAL_LEN_HEAD(value)) {
        // !!! Comment said `fail (Error_Past_End_Raw());`
        Append_Unencoded(mold->series, "\"\"");
        return;
    }

    Sniff_String(ser, idx, &sf);
    if (!GET_MOPT(mold, MOPT_NON_ANSI_PARENED)) sf.paren = 0;

    // Source can be 8 or 16 bits:
    if (unicode) {
        up = UNI_HEAD(ser);
        bp = NULL; // wasteful, but avoids may be used uninitialized warning
    }
    else {
        up = NULL; // wasteful, but avoids may be used uninitialized warning
        bp = BIN_HEAD(ser);
    }

    // If it is a short quoted string, emit it as "string":
    if (len <= MAX_QUOTED_STR && sf.quote == 0 && sf.newline < 3) {

        dp = Prep_Uni_Series(mold, len + sf.newline + sf.escape + sf.paren + sf.chr1e + 2);

        *dp++ = '"';

        for (n = idx; n < VAL_LEN_HEAD(value); n++) {
            c = unicode ? up[n] : cast(REBUNI, bp[n]);
            dp = Emit_Uni_Char(dp, c, GET_MOPT(mold, MOPT_NON_ANSI_PARENED));
        }

        *dp++ = '"';
        *dp = 0;
        return;
    }

    // It is a braced string, emit it as {string}:
    if (!sf.malign) sf.brace_in = sf.brace_out = 0;

    dp = Prep_Uni_Series(mold, len + sf.brace_in + sf.brace_out + sf.escape + sf.paren + sf.chr1e + 2);

    *dp++ = '{';

    for (n = idx; n < VAL_LEN_HEAD(value); n++) {

        c = unicode ? up[n] : cast(REBUNI, bp[n]);

        switch (c) {
        case '{':
        case '}':
            if (sf.malign) {
                *dp++ = '^';
                *dp++ = c;
                break;
            }
        case '\n':
        case '"':
            *dp++ = c;
            break;
        default:
            dp = Emit_Uni_Char(dp, c, GET_MOPT(mold, MOPT_NON_ANSI_PARENED));
        }
    }

    *dp++ = '}';
    *dp = 0;
}


/*
    http://www.blooberry.com/indexdot/html/topics/urlencoding.htm

    Only alphanumerics [0-9a-zA-Z], the special characters $-_.+!*'(),
    and reserved characters used for their reserved purposes may be used
    unencoded within a URL.
*/

static void Mold_Url(const REBVAL *value, REB_MOLD *mold)
{
    REBUNI *dp;
    REBCNT n;
    REBUNI c;
    REBCNT len = VAL_LEN_AT(value);
    REBSER *ser = VAL_SERIES(value);

    // Compute extra space needed for hex encoded characters:
    for (n = VAL_INDEX(value); n < VAL_LEN_HEAD(value); n++) {
        c = GET_ANY_CHAR(ser, n);
        if (IS_URL_ESC(c)) len += 2;
    }

    dp = Prep_Uni_Series(mold, len);

    for (n = VAL_INDEX(value); n < VAL_LEN_HEAD(value); n++) {
        c = GET_ANY_CHAR(ser, n);
        if (IS_URL_ESC(c)) dp = Form_Hex_Esc_Uni(dp, c);  // c => %xx
        else *dp++ = c;
    }

    *dp = 0;
}

static void Mold_File(const REBVAL *value, REB_MOLD *mold)
{
    REBUNI *dp;
    REBCNT n;
    REBUNI c;
    REBCNT len = VAL_LEN_AT(value);
    REBSER *ser = VAL_SERIES(value);

    // Compute extra space needed for hex encoded characters:
    for (n = VAL_INDEX(value); n < VAL_LEN_HEAD(value); n++) {
        c = GET_ANY_CHAR(ser, n);
        if (IS_FILE_ESC(c)) len += 2;
    }

    len++; // room for % at start

    dp = Prep_Uni_Series(mold, len);

    *dp++ = '%';

    for (n = VAL_INDEX(value); n < VAL_LEN_HEAD(value); n++) {
        c = GET_ANY_CHAR(ser, n);
        if (IS_FILE_ESC(c)) dp = Form_Hex_Esc_Uni(dp, c);  // c => %xx
        else *dp++ = c;
    }

    *dp = 0;
}

static void Mold_Tag(const REBVAL *value, REB_MOLD *mold)
{
    Append_Codepoint_Raw(mold->series, '<');
    Insert_String(
        mold->series,
        SER_LEN(mold->series), // "insert" at tail (append)
        VAL_SERIES(value),
        VAL_INDEX(value),
        VAL_LEN_AT(value),
        FALSE
    );
    Append_Codepoint_Raw(mold->series, '>');

}

//
//  Mold_Binary: C
//
void Mold_Binary(const REBVAL *value, REB_MOLD *mold)
{
    REBCNT len = VAL_LEN_AT(value);
    REBSER *out;

    switch (Get_System_Int(SYS_OPTIONS, OPTIONS_BINARY_BASE, 16)) {
    default:
    case 16:
        out = Encode_Base16(value, 0, LOGICAL(len > 32));
        break;
    case 64:
        Append_Unencoded(mold->series, "64");
        out = Encode_Base64(value, 0, LOGICAL(len > 64));
        break;
    case 2:
        Append_Codepoint_Raw(mold->series, '2');
        out = Encode_Base2(value, 0, LOGICAL(len > 8));
        break;
    }

    Emit(mold, "#{E}", out);
    Free_Series(out);
}

static void Mold_All_String(const REBVAL *value, REB_MOLD *mold)
{
    //// ???? move to above Mold_String_Series function????

    Pre_Mold(value, mold); // e.g. #[file! part

    DECLARE_LOCAL (head);
    Move_Value(head, value);
    VAL_INDEX(head) = 0;

    if (IS_BINARY(value))
        Mold_Binary(head, mold);
    else {
        VAL_RESET_HEADER(head, REB_STRING);
        Mold_String_Series(head, mold);
    }

    Post_Mold(value, mold);
}


/***********************************************************************
************************************************************************
**
**  SECTION: Block Series Datatypes
**
************************************************************************
***********************************************************************/

//
//  Mold_Array_At: C
//
void Mold_Array_At(
    REB_MOLD *mold,
    REBARR *array,
    REBCNT index,
    const char *sep
) {
    REBSER *out = mold->series;
    REBOOL line_flag = FALSE; // newline was part of block
    REBOOL had_lines = FALSE;
    RELVAL *value = ARR_AT(array, index);

    if (!sep) sep = "[]";

    if (IS_END(value)) {
        Append_Unencoded(out, sep);
        return;
    }

    // Recursion check:
    if (Find_Same_Array(MOLD_STACK, value) != NOT_FOUND) {
        Emit(mold, "C...C", sep[0], sep[1]);
        return;
    }

    // We don't want to use Init_Block because it will create an implicit
    // managed value, and the incoming series may be from an unmanaged source
    // !!! Review how to avoid needing to put the series into a value
    {
        REBVAL *temp = Alloc_Tail_Array(MOLD_STACK);
        VAL_RESET_HEADER(temp, REB_BLOCK);
        INIT_VAL_ARRAY(temp, array); // copies args
        VAL_INDEX(temp) = 0;
    }

    if (sep[1]) {
        Append_Codepoint_Raw(out, sep[0]);
        mold->indent++;
    }
//  else out->tail--;  // why?????

    value = ARR_AT(array, index);
    while (NOT_END(value)) {
        if (GET_VAL_FLAG(value, VALUE_FLAG_LINE)) {
            if (sep[1] || line_flag) New_Indented_Line(mold);
            had_lines = TRUE;
        }
        line_flag = TRUE;
        Mold_Value(mold, value, TRUE);
        value++;
        if (NOT_END(value))
            Append_Codepoint_Raw(out, (sep[0] == '/') ? '/' : ' ');
    }

    if (sep[1]) {
        mold->indent--;
        if (had_lines)
            New_Indented_Line(mold);
        Append_Codepoint_Raw(out, sep[1]);
    }

    TERM_ARRAY_LEN(MOLD_STACK, ARR_LEN(MOLD_STACK) - 1);
}


static void Mold_Block(const RELVAL *value, REB_MOLD *mold)
{
    const char *sep;
    REBOOL all = GET_MOPT(mold, MOPT_MOLD_ALL);
    REBSER *series = mold->series;
    REBOOL over = FALSE;

    // Optimize when no index needed:
    if (VAL_INDEX(value) == 0 && !IS_MAP(value)) // && (VAL_TYPE(value) <= REB_LIT_PATH))
        all = FALSE;

    // If out of range, do not cause error to avoid error looping.
    if (VAL_INDEX(value) >= VAL_LEN_HEAD(value)) over = TRUE; // Force it into []

    if (all || (over && !IS_BLOCK(value) && !IS_GROUP(value))) {
        SET_FLAG(mold->opts, MOPT_MOLD_ALL);
        Pre_Mold(value, mold); // #[block! part
        //if (over) Append_Unencoded(mold->series, "[]");
        //else
        Append_Codepoint_Raw(mold->series, '[');
        Mold_Array_At(mold, VAL_ARRAY(value), 0, 0);
        Post_Mold(value, mold);
        Append_Codepoint_Raw(mold->series, ']');
    }
    else
    {
        switch(VAL_TYPE(value)) {

        case REB_MAP:
            Pre_Mold(value, mold);
            sep = 0;

        case REB_BLOCK:
            if (GET_MOPT(mold, MOPT_ONLY)) {
                CLR_FLAG(mold->opts, MOPT_ONLY); // only top level
                sep = "\000\000";
            }
            else sep = 0;
            break;

        case REB_GROUP:
            sep = "()";
            break;

        case REB_GET_PATH:
            series = Append_Codepoint_Raw(series, ':');
            sep = "/";
            break;

        case REB_LIT_PATH:
            series = Append_Codepoint_Raw(series, '\'');
            /* fall through */
        case REB_PATH:
        case REB_SET_PATH:
            sep = "/";
            break;
        default:
            sep = NULL;
        }

        if (over) Append_Unencoded(mold->series, sep ? sep : "[]");
        else Mold_Array_At(mold, VAL_ARRAY(value), VAL_INDEX(value), sep);

        if (VAL_TYPE(value) == REB_SET_PATH)
            Append_Codepoint_Raw(series, ':');
    }
}

static void Mold_Simple_Block(REB_MOLD *mold, RELVAL *block, REBCNT len)
{
    // Simple molder for error locations. Series must be valid.
    // Max length in chars must be provided.
    REBCNT start = SER_LEN(mold->series);

    while (NOT_END(block)) {
        if ((SER_LEN(mold->series) - start) > len) break;
        Mold_Value(mold, block, TRUE);
        block++;
        if (NOT_END(block)) Append_Codepoint_Raw(mold->series, ' ');
    }

    // If it's too large, truncate it:
    if ((SER_LEN(mold->series) - start) > len) {
        SET_SERIES_LEN(mold->series, start + len);
        Append_Unencoded(mold->series, "...");
    }
}


static void Form_Array_At(
    REBARR *array,
    REBCNT index,
    REB_MOLD *mold,
    REBCTX *context
) {
    // Form a series (part_mold means mold non-string values):
    REBINT n;
    REBINT len = ARR_LEN(array) - index;

    if (len < 0) len = 0;

    for (n = 0; n < len;) {
        RELVAL *val = ARR_AT(array, index + n);
        REBVAL *wval = NULL;
        if (context && (IS_WORD(val) || IS_GET_WORD(val))) {
            wval = Select_Canon_In_Context(context, VAL_WORD_CANON(val));
            if (wval) val = wval;
        }
        Mold_Value(mold, val, LOGICAL(wval != NULL));
        n++;
        if (GET_MOPT(mold, MOPT_LINES)) {
            Append_Codepoint_Raw(mold->series, LF);
        }
        else {
            // Add a space if needed:
            if (n < len && SER_LEN(mold->series)
                && *UNI_LAST(mold->series) != LF
                && !GET_MOPT(mold, MOPT_TIGHT)
            ){
                Append_Codepoint_Raw(mold->series, ' ');
            }
        }
    }
}


/***********************************************************************
************************************************************************
**
**  SECTION: Special Datatypes
**
************************************************************************
***********************************************************************/


static void Mold_Typeset(const REBVAL *value, REB_MOLD *mold, REBOOL molded)
{
    REBINT n;

    if (molded) {
        Pre_Mold(value, mold);  // #[typeset! or make typeset!
        Append_Codepoint_Raw(mold->series, '[');
    }

#if !defined(NDEBUG)
    if (VAL_KEY_SPELLING(value) != NULL) {
        //
        // In debug builds we're probably more interested in the symbol than
        // the typesets, if we are looking at a PARAMLIST or KEYLIST.
        //
        Append_Unencoded(mold->series, "(");

        REBSTR *spelling = VAL_KEY_SPELLING(value);
        Append_UTF8_May_Fail(
            mold->series, STR_HEAD(spelling), STR_NUM_BYTES(spelling)
        );
        Append_Unencoded(mold->series, ") ");

        // REVIEW: should detect when a lot of types are active and condense
        // only if the number of types is unreasonable (often for keys/params)
        //
        if (TRUE) {
            Append_Unencoded(mold->series, "...");
            goto skip_types;
        }
    }
#endif

    assert(!TYPE_CHECK(value, REB_0)); // REB_0 is used for internal purposes

    // Note that although REB_MAX_VOID is used as an implementation detail for
    // special typesets in function paramlists or context keys to indicate
    // <opt>-style optionality, the "absence of a type" is not generally legal
    // in user typesets.  Only legal "key" typesets (that have symbols).
    //
    assert(
        !TYPE_CHECK(value, REB_MAX_VOID) || VAL_KEY_SPELLING(value) != NULL
    );

    // Convert bits to types.
    //
    for (n = REB_0 + 1; n < REB_MAX; n++) {
        if (TYPE_CHECK(value, cast(enum Reb_Kind, n))) {
            Emit(mold, "+DN ", SYM_DATATYPE_X, Canon(cast(REBSYM, n)));
        }
    }
    Trim_Tail(mold->series, ' ');

#if !defined(NDEBUG)
skip_types:
#endif

    if (molded) {
        //Form_Typeset(value, mold & ~(1<<MOPT_MOLD_ALL));
        Append_Codepoint_Raw(mold->series, ']');
        End_Mold(mold);
    }
}

static void Mold_Function(const REBVAL *value, REB_MOLD *mold)
{
    Pre_Mold(value, mold);

    Append_Codepoint_Raw(mold->series, '[');

    // !!! The system is no longer keeping the spec of functions, in order
    // to focus on a generalized "meta info object" service.  MOLD of
    // functions temporarily uses the word list as a substitute (which
    // drops types)
    //
    REBARR *words_list = List_Func_Words(value, TRUE); // show pure locals
    Mold_Array_At(mold, words_list, 0, 0);
    Free_Array(words_list);

    if (IS_FUNCTION_INTERPRETED(value)) {
        //
        // MOLD is an example of user-facing code that needs to be complicit
        // in the "lie" about the effective bodies of the functions made
        // by the optimized generators FUNC and CLOS...

        REBOOL is_fake;
        REBARR *body = Get_Maybe_Fake_Func_Body(&is_fake, value);

        Mold_Array_At(mold, body, 0, 0);

        if (is_fake) Free_Array(body); // was shallow copy
    }
    else if (IS_FUNCTION_SPECIALIZER(value)) {
        //
        // !!! Interim form of looking at specialized functions... show
        // the frame
        //
        //     >> source first
        //     first: make function! [[aggregate index] [
        //         aggregate: $void
        //         index: 1
        //     ]]
        //
        REBVAL *exemplar = KNOWN(VAL_FUNC_BODY(value));
        Mold_Value(mold, exemplar, TRUE);
    }

    Append_Codepoint_Raw(mold->series, ']');
    End_Mold(mold);
}


static void Mold_Map(const REBVAL *value, REB_MOLD *mold, REBOOL molded)
{
    REBARR *mapser = MAP_PAIRLIST(VAL_MAP(value));
    RELVAL *val;

    // Prevent endless mold loop:
    if (Find_Same_Array(MOLD_STACK, value) != NOT_FOUND) {
        Append_Unencoded(mold->series, "...]");
        return;
    }
    Append_Value(MOLD_STACK, value);

    if (molded) {
        Pre_Mold(value, mold);
        Append_Codepoint_Raw(mold->series, '[');
    }

    // Mold all entries that are set.  As with contexts, void values are not
    // valid entries but indicate the absence of a value.
    //
    mold->indent++;
    for (val = ARR_HEAD(mapser); NOT_END(val) && NOT_END(val+1); val += 2) {
        if (!IS_VOID(val + 1)) {
            if (molded) New_Indented_Line(mold);
            Emit(mold, "V V", val, val+1);
            if (!molded) Append_Codepoint_Raw(mold->series, '\n');
        }
    }
    mold->indent--;

    if (molded) {
        New_Indented_Line(mold);
        Append_Codepoint_Raw(mold->series, ']');
    }

    End_Mold(mold);
    TERM_ARRAY_LEN(MOLD_STACK, ARR_LEN(MOLD_STACK) - 1);
}


static void Form_Object(const REBVAL *value, REB_MOLD *mold)
{
    REBVAL *key = CTX_KEYS_HEAD(VAL_CONTEXT(value));
    REBVAL *var = CTX_VARS_HEAD(VAL_CONTEXT(value));
    REBOOL had_output = FALSE;

    // Prevent endless mold loop:
    if (Find_Same_Array(MOLD_STACK, value) != NOT_FOUND) {
        Append_Unencoded(mold->series, "...]");
        return;
    }

    Append_Value(MOLD_STACK, value);

    // Mold all words and their values:
    for (; NOT_END(key); key++, var++) {
        if (NOT_VAL_FLAG(key, TYPESET_FLAG_HIDDEN)) {
            had_output = TRUE;
            Emit(mold, "N: V\n", VAL_KEY_SPELLING(key), var);
        }
    }

    // Remove the final newline...but only if WE added something to the buffer
    if (had_output) {
        SET_SERIES_LEN(mold->series, SER_LEN(mold->series) - 1);
        TERM_SEQUENCE(mold->series);
    }

    TERM_ARRAY_LEN(MOLD_STACK, ARR_LEN(MOLD_STACK) - 1);
}


static void Mold_Object(const REBVAL *value, REB_MOLD *mold)
{
    REBVAL *keys_head = CTX_KEYS_HEAD(VAL_CONTEXT(value));

    REBVAL *vars_head;
    if (CTX_VARS_UNAVAILABLE(VAL_CONTEXT(value))) {
        //
        // If something like a function call has gone of the stack, the data
        // for the vars will no longer be available.  The keys should still
        // be good, however.
        //
        vars_head = NULL;
    }
    else
        vars_head = CTX_VARS_HEAD(VAL_CONTEXT(value));

    Pre_Mold(value, mold);

    Append_Codepoint_Raw(mold->series, '[');

    // Prevent infinite looping:
    if (Find_Same_Array(MOLD_STACK, value) != NOT_FOUND) {
        Append_Unencoded(mold->series, "...]");
        return;
    }

    Append_Value(MOLD_STACK, value);
    mold->indent++;

    // !!! New experimental Ren-C code for the [[spec][body]] format of the
    // non-evaluative MAKE OBJECT!.

    // First loop: spec block.  This is difficult because unlike functions,
    // objects are dynamically modified with new members added.  If the spec
    // were captured with strings and other data in it as separate from the
    // "keylist" information, it would have to be updated to reflect newly
    // added fields in order to be able to run a corresponding MAKE OBJECT!.
    //
    // To get things started, we aren't saving the original spec that made
    // the object...but regenerate one from the keylist.  If this were done
    // with functions, they would "forget" their help strings in MOLDing.

    New_Indented_Line(mold);
    Append_Codepoint_Raw(mold->series, '[');

    REBVAL *key = keys_head;
    REBVAL *var = vars_head;

    for (; NOT_END(key); var != NULL ? (++key, ++var) : ++key) {
        if (key != keys_head)
            Append_Codepoint_Raw(mold->series, ' ');

        // !!! Feature of hidden words in object specs not yet implemented,
        // but if it paralleled how function specs work it would be SET-WORD!
        //
        DECLARE_LOCAL (any_word);
        Init_Any_Word(
            any_word,
            GET_VAL_FLAG(key, TYPESET_FLAG_HIDDEN) ? REB_SET_WORD : REB_WORD,
            VAL_KEY_SPELLING(key)
        );
        Mold_Value(mold, any_word, TRUE);
    }

    Append_Codepoint_Raw(mold->series, ']');
    New_Indented_Line(mold);
    Append_Codepoint_Raw(mold->series, '[');

    mold->indent++;

    key = keys_head;
    var = vars_head;

    for (; NOT_END(key); var ? (++key, ++var) : ++key) {
        if (GET_VAL_FLAG(key, TYPESET_FLAG_HIDDEN))
            continue; // !!! Should hidden fields be in molded view?

        if (var && IS_VOID(value))
            continue;

        New_Indented_Line(mold);

        REBSTR *spelling = VAL_KEY_SPELLING(key);
        Append_UTF8_May_Fail(
            mold->series, STR_HEAD(spelling), STR_NUM_BYTES(spelling)
        );

        Append_Unencoded(mold->series, ": ");

        if (var)
            Mold_Value(mold, var, TRUE);
        else
            Append_Unencoded(mold->series, ": --optimized out--");
    }

    mold->indent--;
    New_Indented_Line(mold);
    Append_Codepoint_Raw(mold->series, ']');
    mold->indent--;
    New_Indented_Line(mold);
    Append_Codepoint_Raw(mold->series, ']');

    End_Mold(mold);
    TERM_ARRAY_LEN(MOLD_STACK, ARR_LEN(MOLD_STACK) - 1);
}


static void Mold_Error(const REBVAL *value, REB_MOLD *mold, REBOOL molded)
{
    ERROR_VARS *vars;
    REBCTX *context;

    // Protect against recursion. !!!!

    if (molded) {
        Mold_Object(value, mold);
        return;
    }

    context = VAL_CONTEXT(value);
    vars = VAL_ERR_VARS(value);

    // Form: ** <type> Error:
    Emit(mold, "** WS", &vars->type, RM_ERROR_LABEL);

    // Append: error message ARG1, ARG2, etc.
    if (IS_BLOCK(&vars->message))
        Form_Array_At(VAL_ARRAY(&vars->message), 0, mold, context);
    else if (IS_STRING(&vars->message))
        Mold_Value(mold, &vars->message, FALSE);
    else
        Append_Unencoded(mold->series, RM_BAD_ERROR_FORMAT);

    // Form: ** Where: function
    value = &vars->where;
    if (VAL_TYPE(value) != REB_BLANK) {
        Append_Codepoint_Raw(mold->series, '\n');
        Append_Unencoded(mold->series, RM_ERROR_WHERE);
        Mold_Value(mold, value, FALSE);
    }

    // Form: ** Near: location
    value = &vars->nearest;
    if (VAL_TYPE(value) != REB_BLANK) {
        Append_Codepoint_Raw(mold->series, '\n');
        Append_Unencoded(mold->series, RM_ERROR_NEAR);
        if (IS_STRING(value)) // special case: source file line number
            Append_String(mold->series, VAL_SERIES(value), 0, VAL_LEN_HEAD(value));
        else if (IS_BLOCK(value))
            Mold_Simple_Block(mold, VAL_ARRAY_AT(value), 60);
    }
}


/***********************************************************************
************************************************************************
**
**  SECTION: Global Mold Functions
**
************************************************************************
***********************************************************************/

//
//  Mold_Value: C
//
// Mold or form any value to string series tail.
//
void Mold_Value(REB_MOLD *mold, const RELVAL *value, REBOOL molded)
{
    REBYTE buf[60];
    REBINT len;
    REBSER *ser = mold->series;

    if (C_STACK_OVERFLOWING(&len)) Trap_Stack_Overflow();

    assert(SER_WIDE(ser) == sizeof(REBUNI));
    ASSERT_SERIES_TERM(ser);

    if (GET_MOPT(mold, MOPT_LIMIT)) {
        //
        // It's hard to detect the exact moment of tripping over the length
        // limit unless all code paths that add to the mold buffer (e.g.
        // tacking on delimiters etc.) check the limit.  The easier thing
        // to do is check at the end and truncate.  This adds a lot of data
        // wastefully, so short circuit here in the release build.  (Have
        // the debug build keep going to exercise mold on the data.)
        //
        #ifdef NDEBUG
            if (SER_LEN(mold->series) >= mold->limit)
                return;
        #endif
    }

    if (THROWN(value)) {
        // !!! You do not want to see THROWN values leak into user awareness,
        // as they are an implementation detail.  So unless this is debug
        // output, it should be an assert.  Thus REB_MOLD probably needs a
        // "for debug output purposes" switch.
        Emit(mold, "S", "!!! THROWN() -> ");
    }

    // Special handling of string series: {
    if (ANY_STRING(value) && !IS_TAG(value)) {

        // Forming a string:
        if (!molded) {
            Insert_String(
                ser,
                SER_LEN(ser), // "insert" at tail (append)
                VAL_SERIES(value),
                VAL_INDEX(value),
                VAL_LEN_AT(value),
                FALSE
            );
            return;
        }

        // Special format for ALL string series when not at head:
        if (GET_MOPT(mold, MOPT_MOLD_ALL) && VAL_INDEX(value) != 0) {
            Mold_All_String(const_KNOWN(value), mold);
            return;
        }
    }

    switch (VAL_TYPE(value)) {
    case REB_MAX_VOID:
        // Voids should only be molded in debug scenarios
        Append_Unencoded(ser, "&void");
        break;

    case REB_BAR:
        Append_Unencoded(ser, "|");
        break;

    case REB_LIT_BAR:
        Append_Unencoded(ser, "'|");
        break;

    case REB_BLANK:
        Append_Unencoded(ser, "_");
        break;

    case REB_LOGIC:
        Emit(mold, "+N", VAL_LOGIC(value) ? Canon(SYM_TRUE) : Canon(SYM_FALSE));
        break;

    case REB_INTEGER:
        len = Emit_Integer(buf, VAL_INT64(value));
        goto append;

    case REB_DECIMAL:
    case REB_PERCENT:
        len = Emit_Decimal(buf, VAL_DECIMAL(value), IS_PERCENT(value)?DEC_MOLD_PERCENT:0,
            Punctuation[GET_MOPT(mold, MOPT_COMMA_PT) ? PUNCT_COMMA : PUNCT_DOT], mold->digits);
        goto append;

    case REB_MONEY:
        len = Emit_Money(const_KNOWN(value), buf, mold->opts);
        goto append;

    case REB_CHAR:
        Mold_Uni_Char(
            ser, VAL_CHAR(value), molded, GET_MOPT(mold, MOPT_MOLD_ALL)
        );
        break;

    case REB_PAIR:
        len = Emit_Decimal(buf, VAL_PAIR_X(value), DEC_MOLD_MINIMAL, Punctuation[PUNCT_DOT], mold->digits/2);
        Append_Unencoded_Len(ser, s_cast(buf), len);
        Append_Codepoint_Raw(ser, 'x');
        len = Emit_Decimal(buf, VAL_PAIR_Y(value), DEC_MOLD_MINIMAL, Punctuation[PUNCT_DOT], mold->digits/2);
        Append_Unencoded_Len(ser, s_cast(buf), len);
        //Emit(mold, "IxI", VAL_PAIR_X(value), VAL_PAIR_Y(value));
        break;

    case REB_TUPLE:
        len = Emit_Tuple(const_KNOWN(value), buf);
        goto append;

    case REB_TIME:
        //len = Emit_Time(value, buf, Punctuation[GET_MOPT(mold, MOPT_COMMA_PT) ? PUNCT_COMMA : PUNCT_DOT]);
        Emit_Time(mold, const_KNOWN(value));
        break;

    case REB_DATE:
        Emit_Date(mold, const_KNOWN(value));
        break;

    case REB_STRING:
        // FORM happens in top section.
        Mold_String_Series(const_KNOWN(value), mold);
        break;

    case REB_BINARY:
        if (GET_MOPT(mold, MOPT_MOLD_ALL) && VAL_INDEX(value) != 0) {
            Mold_All_String(const_KNOWN(value), mold);
            return;
        }
        Mold_Binary(const_KNOWN(value), mold);
        break;

    case REB_FILE:
        if (VAL_LEN_AT(value) == 0) {
            Append_Unencoded(ser, "%\"\"");
            break;
        }
        Mold_File(const_KNOWN(value), mold);
        break;

    case REB_EMAIL:
    case REB_URL:
        Mold_Url(const_KNOWN(value), mold);
        break;

    case REB_TAG:
        if (GET_MOPT(mold, MOPT_MOLD_ALL) && VAL_INDEX(value) != 0) {
            Mold_All_String(const_KNOWN(value), mold);
            return;
        }
        Mold_Tag(const_KNOWN(value), mold);
        break;

//      Mold_Issue(value, mold);
//      break;

    case REB_BITSET:
        Pre_Mold(value, mold); // #[bitset! or make bitset!
        Mold_Bitset(const_KNOWN(value), mold);
        End_Mold(mold);
        break;

    case REB_IMAGE:
        Pre_Mold(value, mold);
        if (!GET_MOPT(mold, MOPT_MOLD_ALL)) {
            Append_Codepoint_Raw(ser, '[');
            Mold_Image_Data(const_KNOWN(value), mold);
            Append_Codepoint_Raw(ser, ']');
            End_Mold(mold);
        }
        else {
            DECLARE_LOCAL (head);
            Move_Value(head, const_KNOWN(value));
            VAL_INDEX(head) = 0; // mold all of it
            Mold_Image_Data(head, mold);
            Post_Mold(value, mold);
        }
        break;

    case REB_BLOCK:
    case REB_GROUP:
        if (!molded)
            Form_Array_At(VAL_ARRAY(value), VAL_INDEX(value), mold, 0);
        else
            Mold_Block(value, mold);
        break;

    case REB_PATH:
    case REB_SET_PATH:
    case REB_GET_PATH:
    case REB_LIT_PATH:
        Mold_Block(value, mold);
        break;

    case REB_VECTOR:
        Mold_Vector(const_KNOWN(value), mold, molded);
        break;

    case REB_DATATYPE: {
        REBSTR *name = Canon(VAL_TYPE_SYM(value));
    #if !defined(NDEBUG)
        if (LEGACY(OPTIONS_PAREN_INSTEAD_OF_GROUP)) {
            if (VAL_TYPE_KIND(value) == REB_GROUP)
                name = Canon(SYM_PAREN_X); // e_Xclamation point (GROUP!)
        }
    #endif
        if (!molded)
            Emit(mold, "N", name);
        else
            Emit(mold, "+DN", SYM_DATATYPE_X, name);
        break; }

    case REB_TYPESET:
        Mold_Typeset(const_KNOWN(value), mold, molded);
        break;

    case REB_WORD: { // Note: called often
        REBSTR *spelling = VAL_WORD_SPELLING(value);
        Append_UTF8_May_Fail(ser, STR_HEAD(spelling), STR_NUM_BYTES(spelling));
        break;
        }

    case REB_SET_WORD:
        Emit(mold, "W:", value);
        break;

    case REB_GET_WORD:
        Emit(mold, ":W", value);
        break;

    case REB_LIT_WORD:
        Emit(mold, "\'W", value);
        break;

    case REB_REFINEMENT:
        Emit(mold, "/W", value);
        break;

    case REB_ISSUE:
        Emit(mold, "#W", value);
        break;

    case REB_FUNCTION:
        Mold_Function(const_KNOWN(value), mold);
        break;

    case REB_VARARGS:
        Mold_Varargs(const_KNOWN(value), mold);
        break;

    case REB_OBJECT:
    case REB_MODULE:
    case REB_PORT:
    case REB_FRAME:
        if (!molded) Form_Object(const_KNOWN(value), mold);
        else Mold_Object(const_KNOWN(value), mold);
        break;

    case REB_ERROR:
        Mold_Error(const_KNOWN(value), mold, molded);
        break;

    case REB_MAP:
        Mold_Map(const_KNOWN(value), mold, molded);
        break;

    case REB_GOB:
    {
        REBARR *array;
        Pre_Mold(value, mold);
        array = Gob_To_Array(VAL_GOB(value));
        Mold_Array_At(mold, array, 0, 0);
        End_Mold(mold);
        Free_Array(array);
    }
        break;

    case REB_EVENT:
        Mold_Event(const_KNOWN(value), mold);
        break;

    case REB_STRUCT: {
        Pre_Mold(value, mold);

        REBARR *array = Struct_To_Array(VAL_STRUCT(value));
        Mold_Array_At(mold, array, 0, 0);
        Free_Array(array);

        End_Mold(mold);
        break; }

    case REB_LIBRARY: {
        Pre_Mold(value, mold);

        REBCTX *meta = VAL_LIBRARY_META(value);
        if (meta)
            Mold_Object(CTX_VALUE(meta), mold);

        End_Mold(mold);
        break; }

    case REB_HANDLE:
        // Value has no printable form, so just print its name.
        if (!molded) Emit(mold, "?T?", value);
        else Emit(mold, "+T", value);
        break;

    default:
        panic (value);
    }
    goto check_and_return;

append:
    Append_Unencoded_Len(ser, s_cast(buf), len);

check_and_return:
    ASSERT_SERIES_TERM(ser);
}


//
//  Copy_Form_Value: C
//
// Form a value based on the mold opts provided.
//
REBSER *Copy_Form_Value(const RELVAL *value, REBFLGS opts)
{
    REB_MOLD mo;
    CLEARS(&mo);
    mo.opts = opts;

    Push_Mold(&mo);
    Mold_Value(&mo, value, FALSE);
    return Pop_Molded_String(&mo);
}


//
//  Copy_Mold_Value: C
//
// Form a value based on the mold opts provided.
//
REBSER *Copy_Mold_Value(const REBVAL *value, REBFLGS opts)
{
    REB_MOLD mo;
    CLEARS(&mo);
    mo.opts = opts;

    Push_Mold(&mo);
    Mold_Value(&mo, value, TRUE);
    return Pop_Molded_String(&mo);
}


//
//  Form_Reduce_Throws: C
//
// Evaluates each item in a block and forms it, with an optional delimiter.
//
// The special treatment of BLANK! in the source block is to act as an
// opt-out, and the special treatment of BAR! is to act as a line break.
// There's no such thing as a void literal in the incoming block, but if
// an element evaluated to void it is also considered an opt-out, equivalent
// to a BLANK!.
//
// BAR!, BLANK!/void, and CHAR! suppress the delimiter logic.  Hence if you
// are to form `["a" space "b" | () (blank) "c" newline "d" "e"]` with a
// delimiter of ":", you will get back `"a b^/c^/d:e"... where only the
// last interstitial is considered a valid candidate for delimiting.
//
REBOOL Form_Reduce_Throws(
    REBVAL *out,
    REBARR *array,
    REBCNT index,
    REBSPC *specifier,
    const REBVAL *delimiter
) {
    assert(!IS_VOID(delimiter)); // use BLANK! to indicate no delimiting
    if (IS_BAR(delimiter))
        delimiter = ROOT_NEWLINE_CHAR; // BAR! is synonymous to newline here

    REB_MOLD mo;
    CLEARS(&mo);

    Push_Mold(&mo);

    REBFRM f;
    Push_Frame_At(&f, array, index, specifier);

    REBOOL pending = FALSE;

    while (NOT_END(f.value)) {
        if (IS_BLANK(f.value)) { // opt-out
            Fetch_Next_In_Frame(&f);
            continue;
        }

        if (IS_BAR(f.value)) { // newline
            Append_Codepoint_Raw(mo.series, '\n');
            pending = FALSE;
            Fetch_Next_In_Frame(&f);
            continue;
        }

        if (Do_Next_In_Frame_Throws(out, &f)) {
            Drop_Frame(&f);
            return TRUE;
        }

        if (IS_VOID(out) || IS_BLANK(out)) // opt-out
            continue;

        if (IS_BAR(out)) { // newline
            Append_Codepoint_Raw(mo.series, '\n');
            pending = FALSE;
            continue;
        }

        if (IS_CHAR(out)) {
            Append_Codepoint_Raw(mo.series, VAL_CHAR(out));
            pending = FALSE;
        }
        else if (IS_BLANK(delimiter)) // no delimiter
            Mold_Value(&mo, out, FALSE);
        else {
            if (pending)
                Mold_Value(&mo, delimiter, FALSE);

            Mold_Value(&mo, out, FALSE);
            pending = TRUE;
        }
    }

    Drop_Frame(&f);

    Init_String(out, Pop_Molded_String(&mo));

    return FALSE;
}


//
//  Form_Tight_Block: C
//
REBSER *Form_Tight_Block(const REBVAL *blk)
{
    RELVAL *val;

    REB_MOLD mo;
    CLEARS(&mo);

    Push_Mold(&mo);
    for (val = VAL_ARRAY_AT(blk); NOT_END(val); val++)
        Mold_Value(&mo, val, FALSE);

    return Pop_Molded_String(&mo);
}


//
//  Push_Mold: C
//
void Push_Mold(REB_MOLD *mold)
{
#if !defined(NDEBUG)
    //
    // If some kind of Debug_Fmt() happens while this Push_Mold is happening,
    // it will lead to a recursion.  It's necessary to look at the stack in
    // the debugger and figure it out manually.  (e.g. any failures in this
    // function will break the very mechanism by which failure messages
    // are reported.)
    //
    // !!! This isn't ideal.  So if all the routines below guaranteed to
    // use some kind of assert reporting mechanism "lower than mold"
    // (hence "lower than Debug_Fmt") that would be an improvement.
    //
    assert(!TG_Pushing_Mold);
    TG_Pushing_Mold = TRUE;
#endif

    // Series is nulled out on Pop in debug builds to make sure you don't
    // Push the same mold tracker twice (without a Pop)
    //
    assert(!mold->series);

#if !defined(NDEBUG)
    // Sanity check that if they set a limit it wasn't 0.  (Perhaps over the
    // long term it would be okay, but for now we'll consider it a mistake.)
    //
    if (GET_MOPT(mold, MOPT_LIMIT))
        assert(mold->limit != 0);
#endif

    mold->series = UNI_BUF;
    mold->start = SER_LEN(mold->series);

    ASSERT_SERIES_TERM(mold->series);

    if (
        GET_MOPT(mold, MOPT_RESERVE)
        && SER_REST(mold->series) < mold->reserve
    ) {
        // Expand will add to the series length, so we set it back.
        //
        // !!! Should reserve actually leave the length expanded?  Some cases
        // definitely don't want this, others do.  The protocol most
        // compatible with the appending mold is to come back with an
        // empty buffer after a push.
        //
        Expand_Series(mold->series, mold->start, mold->reserve);
        SET_SERIES_LEN(mold->series, mold->start);
    }
    else if (SER_REST(mold->series) - SER_LEN(mold->series) > MAX_COMMON) {
        //
        // If the "extra" space in the series has gotten to be excessive (due
        // to some particularly large mold), back off the space.  But preserve
        // the contents, as there may be important mold data behind the
        // ->start index in the stack!
        //
        Remake_Series(
            mold->series,
            SER_LEN(mold->series) + MIN_COMMON,
            SER_WIDE(mold->series),
            MKS_PRESERVE
        );
    }

    if (GET_MOPT(mold, MOPT_MOLD_ALL))
        mold->digits = MAX_DIGITS;
    else {
        // If there is no notification when the option is changed, this
        // must be retrieved each time.
        //
        // !!! It may be necessary to mold out values before the options
        // block is loaded, and this 'Get_System_Int' is a bottleneck which
        // crashes that in early debugging.  BOOT_ERRORS is sufficient.
        //
        if (PG_Boot_Phase >= BOOT_ERRORS) {
            REBINT idigits = Get_System_Int(
                SYS_OPTIONS, OPTIONS_DECIMAL_DIGITS, MAX_DIGITS
            );
            if (idigits < 0)
                mold->digits = 0;
            else if (idigits > MAX_DIGITS)
                mold->digits = cast(REBCNT, idigits);
            else
                mold->digits = MAX_DIGITS;
        }
        else
            mold->digits = MAX_DIGITS;
    }

#if !defined(NDEBUG)
    TG_Pushing_Mold = FALSE;
#endif
}


//
//  Throttle_Mold: C
//
// Contain a mold's series to its limit (if it has one).
//
void Throttle_Mold(REB_MOLD *mold) {
    if (GET_MOPT(mold, MOPT_LIMIT) && SER_LEN(mold->series) > mold->limit) {
        SET_SERIES_LEN(mold->series, mold->limit - 3); // account for ellipsis
        Append_Unencoded(mold->series, "..."); // adds a null at the tail
    }
}


//
//  Pop_Molded_String_Core: C
//
// When a Push_Mold is started, then string data for the mold is accumulated
// at the tail of the task-global unicode buffer.  Once the molding is done,
// this allows extraction of the string, and resets the buffer to its length
// at the time when the last push began.
//
// Can limit string output to a specified size to prevent long console
// garbage output if MOPT_LIMIT was set in Push_Mold().
//
// If len is END_FLAG then all the string content will be copied, otherwise
// it will be copied up to `len`.  If there are not enough characters then
// the debug build will assert.
//
REBSER *Pop_Molded_String_Core(REB_MOLD *mold, REBCNT len)
{
    REBSER *string;

    assert(mold->series); // if NULL there was no Push_Mold()

    ASSERT_SERIES_TERM(mold->series);
    Throttle_Mold(mold);

    assert(
        (len == UNKNOWN) || (len <= SER_LEN(mold->series) - mold->start)
    );

    // The copy process looks at the characters in range and will make a
    // BYTE_SIZE() target string out of the REBUNIs if possible...
    //
    string = Copy_String_Slimming(
        mold->series,
        mold->start,
        (len == UNKNOWN)
            ? SER_LEN(mold->series) - mold->start
            : len
    );

    // Though the protocol of Mold_Value does terminate, it only does so if
    // it adds content to the buffer.  If we did not terminate when we
    // reset the size, then these no-op molds (e.g. mold of "") would leave
    // whatever value in the terminator spot was there.  This could be
    // addressed by making no-op molds terminate.
    //
    TERM_UNI_LEN(mold->series, mold->start);

    mold->series = NULL;

    return string;
}


//
//  Pop_Molded_UTF8: C
//
// Same as Pop_Molded_String() except gives back the data in UTF8 byte-size
// series form.
//
REBSER *Pop_Molded_UTF8(REB_MOLD *mold)
{
    assert(SER_LEN(mold->series) >= mold->start);

    ASSERT_SERIES_TERM(mold->series);
    Throttle_Mold(mold);

    REBSER *bytes = Make_UTF8_Binary(
        UNI_AT(mold->series, mold->start),
        SER_LEN(mold->series) - mold->start,
        0,
        OPT_ENC_UNISRC
    );
    assert(BYTE_SIZE(bytes));

    TERM_UNI_LEN(mold->series, mold->start);

    mold->series = NULL;
    return bytes;
}


//
//  Drop_Mold_Core: C
//
// When generating a molded string, sometimes it's enough to have access to
// the molded data without actually creating a new series out of it.  If the
// information in the mold has done its job and Pop_Molded_String() is not
// required, just call this to drop back to the state of the last push.
//
void Drop_Mold_Core(REB_MOLD *mold, REBOOL not_pushed_ok)
{
    // The tokenizer can often identify tokens to load by their start and end
    // pointers in the UTF8 data it is loading alone.  However, scanning
    // string escapes is a process that requires converting the actual
    // characters to unicode.  To avoid redoing this work later in the scan,
    // it uses the unicode buffer as a storage space from the tokenization
    // that did UTF-8 decoding of string contents to reuse.
    //
    // Despite this usage, it's desirable to be able to do things like output
    // debug strings or do basic molding in that code.  So to reuse the
    // allocated unicode buffer, it has to properly participate in the mold
    // stack protocol.
    //
    // However, only a few token types use the buffer.  Rather than burden
    // the tokenizer with an additional flag, having a modality to be willing
    // to "drop" a mold that hasn't ever been pushed is the easiest way to
    // avoid intervening.  Drop_Mold_If_Pushed(&mo) macro makes this clearer.
    //
    if (not_pushed_ok && !mold->series) return;

    assert(mold->series); // if NULL there was no Push_Mold

    // When pushed data are to be discarded, mold->series may be unterminated.
    // (Indeed that happens when Scan_Item_Push_Mold returns NULL/0.)
    //
    NOTE_SERIES_MAYBE_TERM(mold->series);

    TERM_UNI_LEN(mold->series, mold->start); // see Pop_Molded_String() notes

    mold->series = NULL;
}


//
//  Init_Mold: C
//
void Init_Mold(REBCNT size)
{
    REBYTE *cp;
    REBYTE c;
    const REBYTE *dc;

    Init_Block(TASK_MOLD_STACK, Make_Array(size/10));
    Init_String(TASK_UNI_BUF, Make_Unicode(size));

    // Create quoted char escape table:
    Char_Escapes = cp = ALLOC_N_ZEROFILL(REBYTE, MAX_ESC_CHAR + 1);
    for (c = '@'; c <= '_'; c++) *cp++ = c;
    Char_Escapes[cast(REBYTE, '\t')] = '-'; // tab
    Char_Escapes[cast(REBYTE, '\n')] = '/'; // line feed
    Char_Escapes[cast(REBYTE, '"')] = '"';
    Char_Escapes[cast(REBYTE, '^')] = '^';

    URL_Escapes = cp = ALLOC_N_ZEROFILL(REBYTE, MAX_URL_CHAR + 1);
    //for (c = 0; c <= MAX_URL_CHAR; c++) if (IS_LEX_DELIMIT(c)) cp[c] = ESC_URL;
    for (c = 0; c <= ' '; c++) cp[c] = ESC_URL | ESC_FILE;
    dc = cb_cast(";%\"()[]{}<>");
    for (c = LEN_BYTES(dc); c > 0; c--) URL_Escapes[*dc++] = ESC_URL | ESC_FILE;
}


//
//  Shutdown_Mold: C
//
void Shutdown_Mold(void)
{
    FREE_N(REBYTE, MAX_ESC_CHAR + 1, Char_Escapes);
    FREE_N(REBYTE, MAX_URL_CHAR + 1, URL_Escapes);
}
