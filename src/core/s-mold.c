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

#include "sys-core.h"
#include <float.h>

#define MAX_QUOTED_STR  50  // max length of "string" before going to { }

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
// This is a general "printf-style" utility function, which R3-Alpha used to
// make some formatting tasks easier.  It was not applied consistently, and
// some callsites avoided using it because it would be ostensibly slower
// than calling the functions directly.
//
void Emit(REB_MOLD *mo, const char *fmt, ...)
{
    REBSER *s = mo->series;
    assert(SER_WIDE(s) == 2);

    va_list va;
    va_start(va, fmt);

    REBYTE ender = '\0';

    for (; *fmt; fmt++) {
        switch (*fmt) {
        case 'W': { // Word symbol
            const REBVAL *any_word = va_arg(va, const REBVAL*);
            REBSTR *spelling = VAL_WORD_SPELLING(any_word);
            Append_UTF8_May_Fail(
                s, STR_HEAD(spelling), STR_NUM_BYTES(spelling)
            );
            break;
        }

        case 'V': // Value
            Mold_Value(mo, va_arg(va, const REBVAL*));
            break;

        case 'S': // String of bytes
            Append_Unencoded(s, va_arg(va, const char *));
            break;

        case 'C': // Char
            Append_Codepoint_Raw(s, va_arg(va, REBCNT));
            break;

        case 'E': { // Series (byte or uni)
            REBSER *src = va_arg(va, REBSER*);
            Insert_String(s, SER_LEN(s), src, 0, SER_LEN(src), FALSE);
            break; }

        case 'I': // Integer
            Append_Int(s, va_arg(va, REBINT));
            break;

        case 'i':
            Append_Int_Pad(s, va_arg(va, REBINT), -9);
            Trim_Tail(s, '0');
            break;

        case '2': // 2 digit int (for time)
            Append_Int_Pad(s, va_arg(va, REBINT), 2);
            break;

        case 'T': {  // Type name
            const REBYTE *bytes = Get_Type_Name(va_arg(va, REBVAL*));
            Append_UTF8_May_Fail(s, bytes, LEN_BYTES(bytes));
            break; }

        case 'N': {  // Symbol name
            REBSTR *spelling = va_arg(va, REBSTR*);
            Append_UTF8_May_Fail(
                s, STR_HEAD(spelling), STR_NUM_BYTES(spelling)
            );
            break; }

        case '+': // Add #[ if mold/all
            if (GET_MOLD_FLAG(mo, MOLD_FLAG_ALL)) {
                Append_Unencoded(s, "#[");
                ender = ']';
            }
            break;

        case 'D': // Datatype symbol: #[type
            if (ender != '\0') {
                REBSTR *canon = Canon(cast(REBSYM, va_arg(va, int)));
                Append_UTF8_May_Fail(
                    s, STR_HEAD(canon), STR_NUM_BYTES(canon)
                );
                Append_Codepoint_Raw(s, ' ');
            }
            else
                va_arg(va, REBCNT); // ignore it
            break;

        default:
            Append_Codepoint_Raw(s, *fmt);
        }
    }

    va_end(va);

    if (ender != '\0')
        Append_Codepoint_Raw(s, ender);
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
    if (series == NULL) {
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

        REBCNT tail = SER_LEN(series);
        EXPAND_SERIES_TAIL(series, len);
        *str = BIN_AT(series, tail);
    }
    return series;
}


//
//  Prep_Uni_Series: C
//
REBUNI *Prep_Uni_Series(REB_MOLD *mo, REBCNT len)
{
    REBCNT tail = SER_LEN(mo->series);

    EXPAND_SERIES_TAIL(mo->series, len);

    return UNI_AT(mo->series, tail);
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
void Pre_Mold(REB_MOLD *mo, const RELVAL *v)
{
    Emit(mo, GET_MOLD_FLAG(mo, MOLD_FLAG_ALL) ? "#[T " : "make T ", v);
}


//
//  End_Mold: C
//
// Finish the mold, depending on /ALL with close block.
//
void End_Mold(REB_MOLD *mo)
{
    if (GET_MOLD_FLAG(mo, MOLD_FLAG_ALL))
        Append_Codepoint_Raw(mo->series, ']');
}


//
//  Post_Mold: C
//
// For series that has an index, add the index for mold/all.
// Add closing block.
//
void Post_Mold(REB_MOLD *mo, const RELVAL *v)
{
    if (VAL_INDEX(v)) {
        Append_Codepoint_Raw(mo->series, ' ');
        Append_Int(mo->series, VAL_INDEX(v) + 1);
    }
    if (GET_MOLD_FLAG(mo, MOLD_FLAG_ALL))
        Append_Codepoint_Raw(mo->series, ']');
}


//
//  New_Indented_Line: C
//
// Create a newline with auto-indent on next line if needed.
//
void New_Indented_Line(REB_MOLD *mo)
{
    // Check output string has content already but no terminator:
    //
    REBUNI *cp;
    if (SER_LEN(mo->series) == 0)
        cp = NULL;
    else {
        cp = UNI_LAST(mo->series);
        if (*cp == ' ' || *cp == '\t')
            *cp = '\n';
        else
            cp = NULL;
    }

    // Add terminator:
    if (cp == NULL)
        Append_Codepoint_Raw(mo->series, '\n');

    // Add proper indentation:
    if (NOT_MOLD_FLAG(mo, MOLD_FLAG_INDENT)) {
        REBINT n;
        for (n = 0; n < mo->indent; n++)
            Append_Unencoded(mo->series, "    ");
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

static void Mold_Or_Form_Uni_Char(
    REBSER *dst,
    REBUNI chr,
    REBOOL form,
    REBOOL parened
){
    REBCNT tail = SER_LEN(dst);

    if (form) {
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


static void Mold_String_Series(REB_MOLD *mo, const RELVAL *v)
{
    REBCNT len = VAL_LEN_AT(v);
    REBSER *series = VAL_SERIES(v);
    REBCNT index = VAL_INDEX(v);

    // Empty string:
    if (index >= VAL_LEN_HEAD(v)) {
        // !!! Comment said `fail (Error_Past_End_Raw());`
        Append_Unencoded(mo->series, "\"\"");
        return;
    }

    REB_STRF sf;
    CLEARS(&sf);
    Sniff_String(series, index, &sf);
    if (NOT_MOLD_FLAG(mo, MOLD_FLAG_NON_ANSI_PARENED))
        sf.paren = 0;

    // Source can be 8 or 16 bits:
    REBYTE *bp;
    REBUNI *up;
    REBOOL unicode = NOT(BYTE_SIZE(series));
    if (unicode) {
        up = UNI_HEAD(series);
        bp = NULL; // wasteful, but avoids may be used uninitialized warning
    }
    else {
        up = NULL; // wasteful, but avoids may be used uninitialized warning
        bp = BIN_HEAD(series);
    }

    // If it is a short quoted string, emit it as "string"
    //
    if (len <= MAX_QUOTED_STR && sf.quote == 0 && sf.newline < 3) {
        REBUNI *dp = Prep_Uni_Series(
            mo,
            len + sf.newline + sf.escape + sf.paren + sf.chr1e + 2
        );

        *dp++ = '"';

        REBCNT n;
        for (n = index; n < VAL_LEN_HEAD(v); n++) {
            REBUNI c = unicode ? up[n] : cast(REBUNI, bp[n]);
            dp = Emit_Uni_Char(
                dp, c, GET_MOLD_FLAG(mo, MOLD_FLAG_NON_ANSI_PARENED)
            );
        }

        *dp++ = '"';
        *dp = 0;
        return;
    }

    // It is a braced string, emit it as {string}:
    if (!sf.malign)
        sf.brace_in = sf.brace_out = 0;

    REBUNI *dp = Prep_Uni_Series(
        mo,
        len + sf.brace_in + sf.brace_out + sf.escape + sf.paren + sf.chr1e + 2
    );

    *dp++ = '{';

    REBCNT n;
    for (n = index; n < VAL_LEN_HEAD(v); n++) {
        REBUNI c = unicode ? up[n] : cast(REBUNI, bp[n]);

        switch (c) {
        case '{':
        case '}':
            if (sf.malign) {
                *dp++ = '^';
                *dp++ = c;
                break;
            }
            // fall through
        case '\n':
        case '"':
            *dp++ = c;
            break;

        default:
            dp = Emit_Uni_Char(
                dp, c, GET_MOLD_FLAG(mo, MOLD_FLAG_NON_ANSI_PARENED)
            );
        }
    }

    *dp++ = '}';
    *dp = '\0';
}


/*
    http://www.blooberry.com/indexdot/html/topics/urlencoding.htm

    Only alphanumerics [0-9a-zA-Z], the special characters $-_.+!*'(),
    and reserved characters used for their reserved purposes may be used
    unencoded within a URL.
*/

static void Mold_Url(REB_MOLD *mo, const RELVAL *v)
{
    REBSER *series = VAL_SERIES(v);
    REBCNT len = VAL_LEN_AT(v);

    // Compute extra space needed for hex encoded characters:
    //
    REBCNT n;
    for (n = VAL_INDEX(v); n < VAL_LEN_HEAD(v); ++n) {
        REBUNI c = GET_ANY_CHAR(series, n);
        if (IS_URL_ESC(c))
            len += 2; // c => %xx
    }

    REBUNI *dp = Prep_Uni_Series(mo, len);

    for (n = VAL_INDEX(v); n < VAL_LEN_HEAD(v); ++n) {
        REBUNI c = GET_ANY_CHAR(series, n);
        if (IS_URL_ESC(c))
            dp = Form_Hex_Esc_Uni(dp, c); // c => %xx
        else
            *dp++ = c;
    }

    *dp = '\0';
}


static void Mold_File(REB_MOLD *mo, const RELVAL *v)
{
    REBSER *series = VAL_SERIES(v);
    REBCNT len = VAL_LEN_AT(v);

    // Compute extra space needed for hex encoded characters:
    //
    REBCNT n;
    for (n = VAL_INDEX(v); n < VAL_LEN_HEAD(v); ++n) {
        REBUNI c = GET_ANY_CHAR(series, n);
        if (IS_FILE_ESC(c))
            len += 2; // %xx is 3 characters instead of 1
    }

    ++len; // room for % at start

    REBUNI *dp = Prep_Uni_Series(mo, len);

    *dp++ = '%';

    for (n = VAL_INDEX(v); n < VAL_LEN_HEAD(v); ++n) {
        REBUNI c = GET_ANY_CHAR(series, n);
        if (IS_FILE_ESC(c))
            dp = Form_Hex_Esc_Uni(dp, c); // c => %xx
        else
            *dp++ = c;
    }

    *dp = '\0';
}


static void Mold_Tag(REB_MOLD *mo, const RELVAL *v)
{
    Append_Codepoint_Raw(mo->series, '<');
    Insert_String(
        mo->series,
        SER_LEN(mo->series), // "insert" at tail (append)
        VAL_SERIES(v),
        VAL_INDEX(v),
        VAL_LEN_AT(v),
        FALSE
    );
    Append_Codepoint_Raw(mo->series, '>');

}


//
//  Mold_Binary: C
//
void Mold_Binary(REB_MOLD *mo, const RELVAL *v)
{
    REBCNT len = VAL_LEN_AT(v);
    REBSER *out;

    switch (Get_System_Int(SYS_OPTIONS, OPTIONS_BINARY_BASE, 16)) {
    default:
    case 16: {
        const REBOOL brk = LOGICAL(len > 32);
        out = Encode_Base16(NULL, v, brk);
        break; }

    case 64: {
        const REBOOL brk = LOGICAL(len > 64);
        Append_Unencoded(mo->series, "64");
        out = Encode_Base64(NULL, v, brk);
        break; }

    case 2: {
        const REBOOL brk = LOGICAL(len > 8);
        Append_Codepoint_Raw(mo->series, '2');
        out = Encode_Base2(NULL, v, brk);
        break; }
    }

    Emit(mo, "#{E}", out);
    Free_Series(out);
}


static void Mold_All_String(REB_MOLD *mo, const RELVAL *v)
{
    //// ???? move to above Mold_String_Series function????

    Pre_Mold(mo, v); // e.g. #[file! part

    DECLARE_LOCAL (head);
    Move_Value(head, const_KNOWN(v));
    VAL_INDEX(head) = 0;

    if (IS_BINARY(v))
        Mold_Binary(mo, head);
    else {
        VAL_RESET_HEADER(head, REB_STRING);
        Mold_String_Series(mo, head);
    }

    Post_Mold(mo, v);
}


// !!! Used for detecting cycles in MOLD
//
static REBCNT Find_Pointer_In_Series(REBSER *s, void *p)
{
    REBCNT index = 0;
    for (; index < SER_LEN(s); ++index) {
        if (*SER_AT(void*, s, index) == p)
            return index;
    }
    return NOT_FOUND;
}

static void Push_Pointer_To_Series(REBSER *s, void *p)
{
    if (SER_FULL(s))
        Extend_Series(s, 8);
    *SER_AT(void*, s, SER_LEN(s)) = p;
    SET_SERIES_LEN(s, SER_LEN(s) + 1);
}

static void Drop_Pointer_From_Series(REBSER *s, void *p)
{
    assert(p == *SER_AT(void*, s, SER_LEN(s) - 1));
    UNUSED(p);
    SET_SERIES_LEN(s, SER_LEN(s) - 1);

    // !!! Could optimize so mold stack is always dynamic, and just use
    // s->content.dynamic.len--
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
    REB_MOLD *mo,
    REBARR *a,
    REBCNT index,
    const char *sep
) {
    if (sep == NULL)
        sep = "[]";

    // Recursion check:
    if (Find_Pointer_In_Series(TG_Mold_Stack, a) != NOT_FOUND) {
        Emit(mo, "C...C", sep[0], sep[1]);
        return;
    }

    Push_Pointer_To_Series(TG_Mold_Stack, a);

    if (sep[1]) {
        Append_Codepoint_Raw(mo->series, sep[0]);
        mo->indent++;
    }

    REBOOL line_flag = FALSE; // newline was part of block
    REBOOL had_lines = FALSE;
    RELVAL *item = ARR_AT(a, index);
    while (NOT_END(item)) {
        if (GET_VAL_FLAG(item, VALUE_FLAG_LINE)) {
            if (sep[1] || line_flag)
                New_Indented_Line(mo);
            had_lines = TRUE;
        }
        line_flag = TRUE;
        Mold_Value(mo, item);
        item++;
        if (NOT_END(item))
            Append_Codepoint_Raw(mo->series, (sep[0] == '/') ? '/' : ' ');
    }

    if (sep[1]) {
        mo->indent--;
        if (had_lines)
            New_Indented_Line(mo);
        Append_Codepoint_Raw(mo->series, sep[1]);
    }

    Drop_Pointer_From_Series(TG_Mold_Stack, a);
}


static void Mold_Block(REB_MOLD *mo, const RELVAL *v)
{
    assert(NOT(IS_MAP(v))); // used to accept MAP!, at some point (?)

    REBOOL all;
    if (VAL_INDEX(v) == 0) { // "&& VAL_TYPE(v) <= REB_LIT_PATH" commented out
        //
        // Optimize when no index needed
        //
        all = FALSE;
    }
    else
        all = GET_MOLD_FLAG(mo, MOLD_FLAG_ALL);

    REBOOL over;
    if (VAL_INDEX(v) >= VAL_LEN_HEAD(v)) {
        //
        // If out of range, do not cause error to avoid error looping.
        //
        over = TRUE; // Force it into []
    }
    else
        over = FALSE;

    if (all || (over && !IS_BLOCK(v) && !IS_GROUP(v))) {
        SET_MOLD_FLAG(mo, MOLD_FLAG_ALL);
        Pre_Mold(mo, v); // #[block! part

        Append_Codepoint_Raw(mo->series, '[');
        Mold_Array_At(mo, VAL_ARRAY(v), 0, 0);
        Post_Mold(mo, v);
        Append_Codepoint_Raw(mo->series, ']');
    }
    else {
        const char *sep;

        switch(VAL_TYPE(v)) {
        case REB_BLOCK:
            if (GET_MOLD_FLAG(mo, MOLD_FLAG_ONLY)) {
                CLEAR_MOLD_FLAG(mo, MOLD_FLAG_ONLY); // only top level
                sep = "\000\000";
            }
            else
                sep = 0;
            break;

        case REB_GROUP:
            sep = "()";
            break;

        case REB_GET_PATH:
            Append_Codepoint_Raw(mo->series, ':');
            sep = "/";
            break;

        case REB_LIT_PATH:
            Append_Codepoint_Raw(mo->series, '\'');
            // fall through
        case REB_PATH:
        case REB_SET_PATH:
            sep = "/";
            break;

        default:
            sep = NULL;
        }

        if (over)
            Append_Unencoded(mo->series, sep ? sep : "[]");
        else
            Mold_Array_At(mo, VAL_ARRAY(v), VAL_INDEX(v), sep);

        if (VAL_TYPE(v) == REB_SET_PATH)
            Append_Codepoint_Raw(mo->series, ':');
    }
}


// Simple molder for error locations. Series must be valid.
// Max length in chars must be provided.
//
static void Mold_Simple_Block(REB_MOLD *mo, RELVAL *block, REBCNT len)
{
    REBCNT start = SER_LEN(mo->series);

    while (NOT_END(block)) {
        if (SER_LEN(mo->series) - start > len)
            break;
        Mold_Value(mo, block);
        block++;
        if (NOT_END(block))
            Append_Codepoint_Raw(mo->series, ' ');
    }

    // If it's too large, truncate it:
    if (SER_LEN(mo->series) - start > len) {
        SET_SERIES_LEN(mo->series, start + len);
        Append_Unencoded(mo->series, "...");
    }
}


static void Form_Array_At(
    REB_MOLD *mo,
    REBARR *array,
    REBCNT index,
    REBCTX *opt_context
) {
    // Form a series (part_mold means mold non-string values):
    REBINT len = ARR_LEN(array) - index;
    if (len < 0)
        len = 0;

    REBINT n;
    for (n = 0; n < len;) {
        RELVAL *item = ARR_AT(array, index + n);
        REBVAL *wval = NULL;
        if (opt_context && (IS_WORD(item) || IS_GET_WORD(item))) {
            wval = Select_Canon_In_Context(opt_context, VAL_WORD_CANON(item));
            if (wval)
                item = wval;
        }
        Mold_Or_Form_Value(mo, item, LOGICAL(wval == NULL));
        n++;
        if (GET_MOLD_FLAG(mo, MOLD_FLAG_LINES)) {
            Append_Codepoint_Raw(mo->series, LF);
        } else {
            // Add a space if needed:
            if (n < len && SER_LEN(mo->series)
                && *UNI_LAST(mo->series) != LF
                && NOT_MOLD_FLAG(mo, MOLD_FLAG_TIGHT)
            ){
                Append_Codepoint_Raw(mo->series, ' ');
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


static void Mold_Or_Form_Typeset(REB_MOLD *mo, const RELVAL *v, REBOOL form)
{
    REBINT n;

    if (NOT(form)) {
        Pre_Mold(mo, v);  // #[typeset! or make typeset!
        Append_Codepoint_Raw(mo->series, '[');
    }

#if !defined(NDEBUG)
    REBSTR *spelling = VAL_KEY_SPELLING(v);
    if (spelling == NULL) {
        //
        // Note that although REB_MAX_VOID is used as an implementation detail
        // for special typesets in function paramlists or context keys to
        // indicate <opt>-style optionality, the "absence of a type" is not
        // generally legal in user typesets.  Only legal "key" typesets
        // (that have symbols).
        //
        assert(NOT(TYPE_CHECK(v, REB_MAX_VOID)));
    }
    else {
        //
        // In debug builds we're probably more interested in the symbol than
        // the typesets, if we are looking at a PARAMLIST or KEYLIST.
        //
        Append_Unencoded(mo->series, "(");

        Append_UTF8_May_Fail(
            mo->series, STR_HEAD(spelling), STR_NUM_BYTES(spelling)
        );
        Append_Unencoded(mo->series, ") ");

        // REVIEW: should detect when a lot of types are active and condense
        // only if the number of types is unreasonable (often for keys/params)
        //
        if (TRUE) {
            Append_Unencoded(mo->series, "...");
            goto skip_types;
        }
    }
#endif

    assert(NOT(TYPE_CHECK(v, REB_0))); // REB_0 is used for internal purposes

    // Convert bits to types.
    //
    for (n = REB_0 + 1; n < REB_MAX; n++) {
        if (TYPE_CHECK(v, cast(enum Reb_Kind, n))) {
            Emit(mo, "+DN ", SYM_DATATYPE_X, Canon(cast(REBSYM, n)));
        }
    }
    Trim_Tail(mo->series, ' ');

#if !defined(NDEBUG)
skip_types:
#endif

    if (NOT(form)) {
        Append_Codepoint_Raw(mo->series, ']');
        End_Mold(mo);
    }
}


static void Mold_Function(REB_MOLD *mo, const RELVAL *v)
{
    Pre_Mold(mo, v);

    Append_Codepoint_Raw(mo->series, '[');

    // !!! The system is no longer keeping the spec of functions, in order
    // to focus on a generalized "meta info object" service.  MOLD of
    // functions temporarily uses the word list as a substitute (which
    // drops types)
    //
    REBARR *words_list = List_Func_Words(v, TRUE); // show pure locals
    Mold_Array_At(mo, words_list, 0, 0);
    Free_Array(words_list);

    if (IS_FUNCTION_INTERPRETED(v)) {
        //
        // MOLD is an example of user-facing code that needs to be complicit
        // in the "lie" about the effective bodies of the functions made
        // by the optimized generators FUNC and PROC...

        REBOOL is_fake;
        REBARR *body = Get_Maybe_Fake_Func_Body(&is_fake, const_KNOWN(v));

        Mold_Array_At(mo, body, 0, 0);

        if (is_fake)
            Free_Array(body); // was shallow copy
    }
    else if (IS_FUNCTION_SPECIALIZER(v)) {
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
        REBVAL *exemplar = KNOWN(VAL_FUNC_BODY(v));
        Mold_Value(mo, exemplar);
    }

    Append_Codepoint_Raw(mo->series, ']');
    End_Mold(mo);
}


static void Mold_Or_Form_Map(REB_MOLD *mo, const RELVAL *v, REBOOL form)
{
    REBMAP *m = VAL_MAP(v);

    // Prevent endless mold loop:
    if (Find_Pointer_In_Series(TG_Mold_Stack, m) != NOT_FOUND) {
        Append_Unencoded(mo->series, "...]");
        return;
    }

    Push_Pointer_To_Series(TG_Mold_Stack, m);

    if (NOT(form)) {
        Pre_Mold(mo, v);
        Append_Codepoint_Raw(mo->series, '[');
    }

    // Mold all entries that are set.  As with contexts, void values are not
    // valid entries but indicate the absence of a value.
    //
    mo->indent++;

    RELVAL *key = ARR_HEAD(MAP_PAIRLIST(m));
    for (; NOT_END(key); key += 2) {
        assert(NOT_END(key + 1)); // value slot must not be END
        if (IS_VOID(key + 1))
            continue; // if value for this key is void, key has been removed

        if (NOT(form))
            New_Indented_Line(mo);
        Emit(mo, "V V", key, key + 1);
        if (form)
            Append_Codepoint_Raw(mo->series, '\n');
    }
    mo->indent--;

    if (NOT(form)) {
        New_Indented_Line(mo);
        Append_Codepoint_Raw(mo->series, ']');
    }

    End_Mold(mo);

    Drop_Pointer_From_Series(TG_Mold_Stack, m);
}


static void Form_Any_Context(REB_MOLD *mo, const RELVAL *v)
{
    REBCTX *c = VAL_CONTEXT(v);

    // Prevent endless mold loop:
    //
    if (Find_Pointer_In_Series(TG_Mold_Stack, c) != NOT_FOUND) {
        Append_Unencoded(mo->series, "...]");
        return;
    }
    Push_Pointer_To_Series(TG_Mold_Stack, c);

    // Mold all words and their values:
    //
    REBVAL *key = CTX_KEYS_HEAD(c);
    REBVAL *var = CTX_VARS_HEAD(c);
    REBOOL had_output = FALSE;
    for (; NOT_END(key); key++, var++) {
        if (NOT_VAL_FLAG(key, TYPESET_FLAG_HIDDEN)) {
            had_output = TRUE;
            Emit(mo, "N: V\n", VAL_KEY_SPELLING(key), var);
        }
    }

    // Remove the final newline...but only if WE added something to the buffer
    //
    if (had_output) {
        SET_SERIES_LEN(mo->series, SER_LEN(mo->series) - 1);
        TERM_SEQUENCE(mo->series);
    }

    Drop_Pointer_From_Series(TG_Mold_Stack, c);
}


static void Mold_Any_Context(REB_MOLD *mo, const RELVAL *v)
{
    REBCTX *c = VAL_CONTEXT(v);

    Pre_Mold(mo, v);

    Append_Codepoint_Raw(mo->series, '[');

    // Prevent infinite looping:
    //
    if (Find_Pointer_In_Series(TG_Mold_Stack, c) != NOT_FOUND) {
        Append_Unencoded(mo->series, "...]");
        return;
    }
    Push_Pointer_To_Series(TG_Mold_Stack, c);

    mo->indent++;

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

    New_Indented_Line(mo);
    Append_Codepoint_Raw(mo->series, '[');

    REBVAL *keys_head = CTX_KEYS_HEAD(c);
    REBVAL *vars_head;
    if (CTX_VARS_UNAVAILABLE(VAL_CONTEXT(v))) {
        //
        // If something like a function call has gone of the stack, the data
        // for the vars will no longer be available.  The keys should still
        // be good, however.
        //
        vars_head = NULL;
    }
    else
        vars_head = CTX_VARS_HEAD(VAL_CONTEXT(v));

    REBVAL *key = keys_head;
    for (; NOT_END(key); ++key) {
        if (GET_VAL_FLAG(key, TYPESET_FLAG_HIDDEN))
            continue;

        if (key != keys_head)
            Append_Codepoint_Raw(mo->series, ' ');

        DECLARE_LOCAL (any_word);
        Init_Any_Word(any_word, REB_WORD, VAL_KEY_SPELLING(key));
        Mold_Value(mo, any_word);
    }

    Append_Codepoint_Raw(mo->series, ']');
    New_Indented_Line(mo);
    Append_Codepoint_Raw(mo->series, '[');

    mo->indent++;

    key = keys_head;

    REBVAL *var = vars_head;

    for (; NOT_END(key); var ? (++key, ++var) : ++key) {
        if (GET_VAL_FLAG(key, TYPESET_FLAG_HIDDEN))
            continue; // !!! Should hidden fields be in molded view?

        // Having the key mentioned in the spec and then not being assigned
        // a value in the body is how voids are denoted.
        //
        if (var && IS_VOID(var))
            continue;

        New_Indented_Line(mo);

        REBSTR *spelling = VAL_KEY_SPELLING(key);
        Append_UTF8_May_Fail(
            mo->series, STR_HEAD(spelling), STR_NUM_BYTES(spelling)
        );

        Append_Unencoded(mo->series, ": ");

        if (var)
            Mold_Value(mo, var);
        else
            Append_Unencoded(mo->series, ": --optimized out--");
    }

    mo->indent--;
    New_Indented_Line(mo);
    Append_Codepoint_Raw(mo->series, ']');
    mo->indent--;
    New_Indented_Line(mo);
    Append_Codepoint_Raw(mo->series, ']');

    End_Mold(mo);

    Drop_Pointer_From_Series(TG_Mold_Stack, c);
}


static void Mold_Or_Form_Error(REB_MOLD *mo, const RELVAL *v, REBOOL form)
{
    // Protect against recursion. !!!!
    //
    if (NOT(form)) {
        Mold_Any_Context(mo, v);
        return;
    }

    REBCTX *error = VAL_CONTEXT(v);
    ERROR_VARS *vars = ERR_VARS(error);

    // Form: ** <type> Error:
    if (IS_BLANK(&vars->type))
        Emit(mo, "** S", RM_ERROR_LABEL);
    else {
        assert(IS_WORD(&vars->type));
        Emit(mo, "** W S", &vars->type, RM_ERROR_LABEL);
    }

    // Append: error message ARG1, ARG2, etc.
    if (IS_BLOCK(&vars->message))
        Form_Array_At(mo, VAL_ARRAY(&vars->message), 0, error);
    else if (IS_STRING(&vars->message))
        Form_Value(mo, &vars->message);
    else
        Append_Unencoded(mo->series, RM_BAD_ERROR_FORMAT);

    // Form: ** Where: function
    REBVAL *where = KNOWN(&vars->where);
    if (NOT(IS_BLANK(where))) {
        Append_Codepoint_Raw(mo->series, '\n');
        Append_Unencoded(mo->series, RM_ERROR_WHERE);
        Form_Value(mo, where);
    }

    // Form: ** Near: location
    REBVAL *nearest = KNOWN(&vars->nearest);
    if (NOT(IS_BLANK(nearest))) {
        Append_Codepoint_Raw(mo->series, '\n');
        Append_Unencoded(mo->series, RM_ERROR_NEAR);

        if (IS_STRING(nearest)) {
            //
            // !!! The scanner puts strings into the near information in order
            // to say where the file and line of the scan problem was.  This
            // seems better expressed as an explicit argument to the scanner
            // error, because otherwise it obscures the LOAD call where the
            // scanner was invoked.  Review.
            //
            Append_String(
                mo->series, VAL_SERIES(nearest), 0, VAL_LEN_HEAD(nearest)
            );
        }
        else if (IS_BLOCK(nearest))
            Mold_Simple_Block(mo, VAL_ARRAY_AT(nearest), 60);
        else
            Append_Unencoded(mo->series, RM_BAD_ERROR_FORMAT);
    }

    // Form: ** File: filename
    //
    // !!! In order to conserve space in the system, filenames are interned.
    // Although interned strings are GC'd when no longer referenced, they can
    // only be used in ANY-WORD! values at the moment, so the filename is
    // not a FILE!.
    //
    REBVAL *file = KNOWN(&vars->file);
    if (NOT(IS_BLANK(file))) {
        Append_Codepoint_Raw(mo->series, '\n');
        Append_Unencoded(mo->series, RM_ERROR_FILE);
        if (IS_WORD(file))
            Form_Value(mo, file);
        else
            Append_Unencoded(mo->series, RM_BAD_ERROR_FORMAT);
    }

    // Form: ** Line: line-number
    REBVAL *line = KNOWN(&vars->line);
    if (NOT(IS_BLANK(line))) {
        Append_Codepoint_Raw(mo->series, '\n');
        Append_Unencoded(mo->series, RM_ERROR_LINE);
        if (IS_INTEGER(line))
            Form_Value(mo, line);
        else
            Append_Unencoded(mo->series, RM_BAD_ERROR_FORMAT);
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
//  Mold_Or_Form_Value: C
//
// Mold or form any value to string series tail.
//
void Mold_Or_Form_Value(REB_MOLD *mo, const RELVAL *v, REBOOL form)
{
    REBSER *s = mo->series;
    assert(SER_WIDE(s) == sizeof(REBUNI));
    ASSERT_SERIES_TERM(s);

    if (C_STACK_OVERFLOWING(&s))
        Trap_Stack_Overflow();

    if (GET_MOLD_FLAG(mo, MOLD_FLAG_LIMIT)) {
        //
        // It's hard to detect the exact moment of tripping over the length
        // limit unless all code paths that add to the mold buffer (e.g.
        // tacking on delimiters etc.) check the limit.  The easier thing
        // to do is check at the end and truncate.  This adds a lot of data
        // wastefully, so short circuit here in the release build.  (Have
        // the debug build keep going to exercise mold on the data.)
        //
    #ifdef NDEBUG
        if (SER_LEN(s) >= mo->limit)
            return;
    #endif
    }

    if (THROWN(v)) {
        //
        // !!! You do not want to see THROWN values leak into user awareness,
        // as they are an implementation detail.  Someone might explicitly
        // PROBE() a thrown value, however.
        //
    #if defined(NDEBUG)
        panic (v);
    #else
        printf("!!! Request to MOLD or FORM a THROWN() value !!!\n");
        Append_Unencoded(s, "!!!THROWN(");
        debug_break(); // don't crash if under a debugger, just "pause"
    #endif
    }

    // Special handling of string series: {
    if (ANY_STRING(v) && NOT(IS_TAG(v))) {
        if (form) {
            Insert_String(
                s,
                SER_LEN(s), // "insert" at tail (append)
                VAL_SERIES(v),
                VAL_INDEX(v),
                VAL_LEN_AT(v),
                FALSE
            );
            return;
        }

        // Special format for ALL string series when not at head:
        if (GET_MOLD_FLAG(mo, MOLD_FLAG_ALL) && VAL_INDEX(v) != 0) {
            Mold_All_String(mo, v);
            return;
        }
    }

    switch (VAL_TYPE(v)) {
    case REB_0:
        // REB_0 is reserved for special purposes, and should only be molded
        // in debug scenarios.
        //
    #if defined(NDEBUG)
        panic (v);
    #else
        printf("!!! Request to MOLD or FORM a REB_0 value !!!\n");
        Append_Unencoded(s, "!!!REB_0!!!");
        debug_break(); // don't crash if under a debugger, just "pause"
    #endif
        break;

    case REB_BAR:
        Append_Unencoded(s, "|");
        break;

    case REB_LIT_BAR:
        Append_Unencoded(s, "'|");
        break;

    case REB_BLANK:
        Append_Unencoded(s, "_");
        break;

    case REB_LOGIC:
        Emit(mo, "+N", VAL_LOGIC(v) ? Canon(SYM_TRUE) : Canon(SYM_FALSE));
        break;

    case REB_INTEGER: {
        REBYTE buf[60];
        REBINT len = Emit_Integer(buf, VAL_INT64(v));
        Append_Unencoded_Len(s, s_cast(buf), len);
        break; }

    case REB_DECIMAL:
    case REB_PERCENT: {
        REBYTE buf[60];
        REBINT len = Emit_Decimal(
            buf,
            VAL_DECIMAL(v),
            IS_PERCENT(v) ? DEC_MOLD_PERCENT : 0,
            Punctuation[GET_MOLD_FLAG(mo, MOLD_FLAG_COMMA_PT)
                ? PUNCT_COMMA
                : PUNCT_DOT],
            mo->digits
        );
        Append_Unencoded_Len(s, s_cast(buf), len);
        break; }

    case REB_MONEY: {
        REBYTE buf[60];
        REBINT len = Emit_Money(buf, v, mo->opts);
        Append_Unencoded_Len(s, s_cast(buf), len);
        break; }

    case REB_CHAR:
        Mold_Or_Form_Uni_Char(
            s, VAL_CHAR(v), form, GET_MOLD_FLAG(mo, MOLD_FLAG_ALL)
        );
        break;

    case REB_PAIR: {
        REBYTE buf[60];
        REBINT len = Emit_Decimal(
            buf,
            VAL_PAIR_X(v),
            DEC_MOLD_MINIMAL,
            Punctuation[PUNCT_DOT],
            mo->digits / 2
        );
        Append_Unencoded_Len(s, s_cast(buf), len);
        Append_Codepoint_Raw(s, 'x');
        len = Emit_Decimal(
            buf,
            VAL_PAIR_Y(v),
            DEC_MOLD_MINIMAL,
            Punctuation[PUNCT_DOT],
            mo->digits / 2
        );
        Append_Unencoded_Len(s, s_cast(buf), len);
        break; }

    case REB_TUPLE: {
        REBYTE buf[60];
        REBINT len = Emit_Tuple(const_KNOWN(v), buf);
        Append_Unencoded_Len(s, s_cast(buf), len);
        break; }

    case REB_TIME:
        Emit_Time(mo, v);
        break;

    case REB_DATE:
        Emit_Date(mo, v);
        break;

    case REB_STRING:
        // FORM happens in top section.
        Mold_String_Series(mo, v);
        break;

    case REB_BINARY:
        if (GET_MOLD_FLAG(mo, MOLD_FLAG_ALL) && VAL_INDEX(v) != 0) {
            Mold_All_String(mo, v);
            return;
        }
        Mold_Binary(mo, v);
        break;

    case REB_FILE:
        if (VAL_LEN_AT(v) == 0) {
            Append_Unencoded(s, "%\"\"");
            break;
        }
        Mold_File(mo, v);
        break;

    case REB_EMAIL:
    case REB_URL:
        Mold_Url(mo, v);
        break;

    case REB_TAG:
        if (GET_MOLD_FLAG(mo, MOLD_FLAG_ALL) && VAL_INDEX(v) != 0) {
            Mold_All_String(mo, v);
            return;
        }
        Mold_Tag(mo, v);
        break;

    case REB_BITSET:
        Pre_Mold(mo, v); // #[bitset! or make bitset!
        Mold_Bitset(mo, v);
        End_Mold(mo);
        break;

    case REB_IMAGE:
        Pre_Mold(mo, v);
        if (GET_MOLD_FLAG(mo, MOLD_FLAG_ALL)) {
            DECLARE_LOCAL (head);
            Move_Value(head, const_KNOWN(v));
            VAL_INDEX(head) = 0; // mold all of it
            Mold_Image_Data(head, mo);
            Post_Mold(mo, v);
        }
        else {
            Append_Codepoint_Raw(s, '[');
            Mold_Image_Data(const_KNOWN(v), mo);
            Append_Codepoint_Raw(s, ']');
            End_Mold(mo);
        }
        break;

    case REB_BLOCK:
    case REB_GROUP:
        if (form)
            Form_Array_At(mo, VAL_ARRAY(v), VAL_INDEX(v), 0);
        else
            Mold_Block(mo, v);
        break;

    case REB_PATH:
    case REB_SET_PATH:
    case REB_GET_PATH:
    case REB_LIT_PATH:
        Mold_Block(mo, v);
        break;

    case REB_VECTOR:
        Mold_Or_Form_Vector(mo, v, form);
        break;

    case REB_DATATYPE: {
        REBSTR *name = Canon(VAL_TYPE_SYM(v));
    #if !defined(NDEBUG)
        if (LEGACY(OPTIONS_PAREN_INSTEAD_OF_GROUP)) {
            if (VAL_TYPE_KIND(v) == REB_GROUP)
                name = Canon(SYM_PAREN_X); // e_Xclamation point (PAREN!)
        }
    #endif
        if (form)
            Emit(mo, "N", name);
        else
            Emit(mo, "+DN", SYM_DATATYPE_X, name);
        break; }

    case REB_TYPESET:
        Mold_Or_Form_Typeset(mo, v, form);
        break;

    case REB_WORD: { // Note: called often
        REBSTR *spelling = VAL_WORD_SPELLING(v);
        Append_UTF8_May_Fail(s, STR_HEAD(spelling), STR_NUM_BYTES(spelling));
        break; }

    case REB_SET_WORD:
        Emit(mo, "W:", v);
        break;

    case REB_GET_WORD:
        Emit(mo, ":W", v);
        break;

    case REB_LIT_WORD:
        Emit(mo, "\'W", v);
        break;

    case REB_REFINEMENT:
        Emit(mo, "/W", v);
        break;

    case REB_ISSUE:
        Emit(mo, "#W", v);
        break;

    case REB_FUNCTION:
        Mold_Function(mo, v);
        break;

    case REB_VARARGS:
        Mold_Varargs(mo, v);
        break;

    case REB_OBJECT:
    case REB_MODULE:
    case REB_PORT:
    case REB_FRAME:
        if (form)
            Form_Any_Context(mo, v);
        else
            Mold_Any_Context(mo, v);
        break;

    case REB_ERROR:
        Mold_Or_Form_Error(mo, v, form);
        break;

    case REB_MAP:
        Mold_Or_Form_Map(mo, v, form);
        break;

    case REB_GOB: {
        Pre_Mold(mo, v);

        REBARR *array = Gob_To_Array(VAL_GOB(v));
        Mold_Array_At(mo, array, 0, 0);
        Free_Array(array);

        End_Mold(mo);
        break; }

    case REB_EVENT:
        Mold_Event(mo, v);
        break;

    case REB_STRUCT: {
        Pre_Mold(mo, v);

        REBARR *array = Struct_To_Array(VAL_STRUCT(v));
        Mold_Array_At(mo, array, 0, 0);
        Free_Array(array);

        End_Mold(mo);
        break; }

    case REB_LIBRARY: {
        Pre_Mold(mo, v);

        REBCTX *meta = VAL_LIBRARY_META(v);
        if (meta)
            Mold_Any_Context(mo, CTX_VALUE(meta));

        End_Mold(mo);
        break; }

    case REB_HANDLE:
        // Value has no printable form, so just print its name.
        if (form)
            Emit(mo, "?T?", v);
        else
            Emit(mo, "+T", v);
        break;

    case REB_MAX_VOID:
        //
        // Voids should only be molded out in debug scenarios, but this still
        // happens a lot, e.g. PROBE() of context arrays when they have unset
        // variables.  This happens so often in debug builds, in fact, that a
        // debug_break() here would be very annoying (the method used for
        // REB_0 and THROWN() items)
        //
    #ifdef NDEBUG
        panic (v);
    #else
        printf("!!! Request to MOLD or FORM a void value !!!\n");
        Append_Unencoded(s, "!!!void!!!");
    #endif
        break;

    default:
        panic (v);
    }

#if !defined(NDEBUG)
    if (THROWN(v))
        Append_Unencoded(s, ")!!!"); // close the "!!!THROWN(" we started
#endif

    ASSERT_SERIES_TERM(s);
}


//
//  Copy_Mold_Or_Form_Value: C
//
// Form a value based on the mold opts provided.
//
REBSER *Copy_Mold_Or_Form_Value(const RELVAL *v, REBFLGS opts, REBOOL form)
{
    DECLARE_MOLD (mo);
    mo->opts = opts;

    Push_Mold(mo);
    Mold_Or_Form_Value(mo, v, form);
    return Pop_Molded_String(mo);
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

    DECLARE_MOLD (mo);

    Push_Mold(mo);

    DECLARE_FRAME (f);
    Push_Frame_At(f, array, index, specifier, DO_FLAG_NORMAL);

    REBOOL pending = FALSE;

    while (NOT_END(f->value)) {
        if (IS_BLANK(f->value)) { // opt-out
            Fetch_Next_In_Frame(f);
            continue;
        }

        if (IS_BAR(f->value)) { // newline
            Append_Codepoint_Raw(mo->series, '\n');
            pending = FALSE;
            Fetch_Next_In_Frame(f);
            continue;
        }

        if (Do_Next_In_Frame_Throws(out, f)) {
            Drop_Frame(f);
            return TRUE;
        }

        if (IS_VOID(out) || IS_BLANK(out)) // opt-out
            continue;

        if (IS_BAR(out)) { // newline
            Append_Codepoint_Raw(mo->series, '\n');
            pending = FALSE;
            continue;
        }

        if (IS_CHAR(out)) {
            Append_Codepoint_Raw(mo->series, VAL_CHAR(out));
            pending = FALSE;
        }
        else if (IS_BLANK(delimiter)) // no delimiter
            Form_Value(mo, out);
        else {
            if (pending)
                Form_Value(mo, delimiter);

            Form_Value(mo, out);
            pending = TRUE;
        }
    }

    Drop_Frame(f);

    Init_String(out, Pop_Molded_String(mo));

    return FALSE;
}


//
//  Form_Tight_Block: C
//
REBSER *Form_Tight_Block(const REBVAL *blk)
{
    DECLARE_MOLD (mo);

    Push_Mold(mo);

    RELVAL *item;
    for (item = VAL_ARRAY_AT(blk); NOT_END(item); ++item)
        Form_Value(mo, item);

    return Pop_Molded_String(mo);
}


//
//  Push_Mold: C
//
void Push_Mold(REB_MOLD *mo)
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
    assert(mo->series == NULL);

#if !defined(NDEBUG)
    // Sanity check that if they set a limit it wasn't 0.  (Perhaps over the
    // long term it would be okay, but for now we'll consider it a mistake.)
    //
    if (GET_MOLD_FLAG(mo, MOLD_FLAG_LIMIT))
        assert(mo->limit != 0);
#endif

    REBSER *s = mo->series = UNI_BUF;
    mo->start = SER_LEN(s);

    ASSERT_SERIES_TERM(s);

    if (GET_MOLD_FLAG(mo, MOLD_FLAG_RESERVE) && SER_REST(s) < mo->reserve) {
        //
        // Expand will add to the series length, so we set it back.
        //
        // !!! Should reserve actually leave the length expanded?  Some cases
        // definitely don't want this, others do.  The protocol most
        // compatible with the appending mold is to come back with an
        // empty buffer after a push.
        //
        Expand_Series(s, mo->start, mo->reserve);
        SET_SERIES_LEN(s, mo->start);
    }
    else if (SER_REST(s) - SER_LEN(s) > MAX_COMMON) {
        //
        // If the "extra" space in the series has gotten to be excessive (due
        // to some particularly large mold), back off the space.  But preserve
        // the contents, as there may be important mold data behind the
        // ->start index in the stack!
        //
        Remake_Series(
            s,
            SER_LEN(s) + MIN_COMMON,
            SER_WIDE(s),
            NODE_FLAG_NODE // NODE_FLAG_NODE means preserve the data
        );
    }

    if (GET_MOLD_FLAG(mo, MOLD_FLAG_ALL))
        mo->digits = MAX_DIGITS;
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
                mo->digits = 0;
            else if (idigits > MAX_DIGITS)
                mo->digits = cast(REBCNT, idigits);
            else
                mo->digits = MAX_DIGITS;
        }
        else
            mo->digits = MAX_DIGITS;
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
void Throttle_Mold(REB_MOLD *mo) {
    if (NOT_MOLD_FLAG(mo, MOLD_FLAG_LIMIT))
        return;

    if (SER_LEN(mo->series) > mo->limit) {
        SET_SERIES_LEN(mo->series, mo->limit - 3); // account for ellipsis
        Append_Unencoded(mo->series, "..."); // adds a null at the tail
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
// garbage output if MOLD_FLAG_LIMIT was set in Push_Mold().
//
// If len is END_FLAG then all the string content will be copied, otherwise
// it will be copied up to `len`.  If there are not enough characters then
// the debug build will assert.
//
REBSER *Pop_Molded_String_Core(REB_MOLD *mo, REBCNT len)
{
    assert(mo->series); // if NULL there was no Push_Mold()

    ASSERT_SERIES_TERM(mo->series);
    Throttle_Mold(mo);

    assert(
        (len == UNKNOWN) || (len <= SER_LEN(mo->series) - mo->start)
    );

    // The copy process looks at the characters in range and will make a
    // BYTE_SIZE() target string out of the REBUNIs if possible...
    //
    REBSER *result = Copy_String_Slimming(
        mo->series,
        mo->start,
        (len == UNKNOWN)
            ? SER_LEN(mo->series) - mo->start
            : len
    );

    // Though the protocol of Mold_Value does terminate, it only does so if
    // it adds content to the buffer.  If we did not terminate when we
    // reset the size, then these no-op molds (e.g. mold of "") would leave
    // whatever value in the terminator spot was there.  This could be
    // addressed by making no-op molds terminate.
    //
    TERM_UNI_LEN(mo->series, mo->start);

    mo->series = NULL;

    return result;
}


//
//  Pop_Molded_UTF8: C
//
// Same as Pop_Molded_String() except gives back the data in UTF8 byte-size
// series form.
//
REBSER *Pop_Molded_UTF8(REB_MOLD *mo)
{
    assert(SER_LEN(mo->series) >= mo->start);

    ASSERT_SERIES_TERM(mo->series);
    Throttle_Mold(mo);

    REBSER *bytes = Make_UTF8_Binary(
        UNI_AT(mo->series, mo->start),
        SER_LEN(mo->series) - mo->start,
        0,
        OPT_ENC_UNISRC
    );
    assert(BYTE_SIZE(bytes));

    TERM_UNI_LEN(mo->series, mo->start);

    mo->series = NULL;
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
void Drop_Mold_Core(REB_MOLD *mo, REBOOL not_pushed_ok)
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
    // avoid intervening.  Drop_Mold_If_Pushed(mo) macro makes this clearer.
    //
    if (not_pushed_ok && mo->series == NULL)
        return;

    assert(mo->series != NULL); // if NULL there was no Push_Mold

    // When pushed data are to be discarded, mo->series may be unterminated.
    // (Indeed that happens when Scan_Item_Push_Mold returns NULL/0.)
    //
    NOTE_SERIES_MAYBE_TERM(mo->series);

    TERM_UNI_LEN(mo->series, mo->start); // see Pop_Molded_String() notes

    mo->series = NULL;
}


//
//  Startup_Mold: C
//
void Startup_Mold(REBCNT size)
{
    REBYTE *cp;
    REBYTE c;
    const REBYTE *dc;

    TG_Mold_Stack = Make_Series(10, sizeof(void*));

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
    Free_Series(TG_Mold_Stack);

    FREE_N(REBYTE, MAX_ESC_CHAR + 1, Char_Escapes);
    FREE_N(REBYTE, MAX_URL_CHAR + 1, URL_Escapes);
}
