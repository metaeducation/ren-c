//
//  File: %t-string.c
//  Summary: "string related datatypes"
//  Section: datatypes
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
//=////////////////////////////////////////////////////////////////////////=//
//

#include "sys-core.h"

#include "sys-int-funcs.h"

#include "datatypes/sys-money.h"

#define MAX_QUOTED_STR  50  // max length of "string" before going to { }

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


//
//  CT_String: C
//
REBINT CT_String(const REBCEL *a, const REBCEL *b, REBINT mode)
{
    assert(ANY_STRING_KIND(CELL_KIND(a)));
    assert(ANY_STRING_KIND(CELL_KIND(b)));

    REBINT num = Compare_String_Vals(a, b, mode != 1);

    if (mode >= 0) return (num == 0) ? 1 : 0;
    if (mode == -1) return (num >= 0) ? 1 : 0;
    return (num > 0) ? 1 : 0;
}


/***********************************************************************
**
**  Local Utility Functions
**
***********************************************************************/


static void swap_chars(REBVAL *val1, REBVAL *val2)
{
    REBSTR *s1 = VAL_STRING(val1);
    REBSTR *s2 = VAL_STRING(val2);

    REBUNI c1 = GET_CHAR_AT(s1, VAL_INDEX(val1));
    REBUNI c2 = GET_CHAR_AT(s2, VAL_INDEX(val2));

    SET_CHAR_AT(s1, VAL_INDEX(val1), c2);
    SET_CHAR_AT(s2, VAL_INDEX(val2), c1);
}

static void reverse_binary(REBVAL *v, REBCNT len)
{
    REBYTE *bp = VAL_BIN_AT(v);

    REBCNT n = 0;
    REBCNT m = len - 1;
    for (; n < len / 2; n++, m--) {
        REBYTE b = bp[n];
        bp[n] = bp[m];
        bp[m] = b;
    }
}


static void reverse_string(REBVAL *v, REBCNT len)
{
    if (len == 0)
        return; // if non-zero, at least one character in the string

    if (Is_String_Definitely_ASCII(v))
        reverse_binary(v, len);
    else {
        // !!! This is an inefficient method for reversing strings with
        // variable size codepoints.  Better way could work in place:
        //
        // https://stackoverflow.com/q/199260/

        DECLARE_MOLD (mo);
        Push_Mold(mo);

        REBCNT val_len_head = VAL_LEN_HEAD(v);

        REBSTR *s = VAL_STRING(v);
        REBCHR(const*) up = STR_LAST(s);  // last exists due to len != 0
        REBCNT n;
        for (n = 0; n < len; ++n) {
            REBUNI c;
            up = BACK_CHR(&c, up);
            Append_Codepoint(mo->series, c);
        }

        DECLARE_LOCAL (temp);
        Init_Text(temp, Pop_Molded_String(mo));

        // Effectively do a CHANGE/PART to overwrite the reversed portion of
        // the string (from the input value's index to the tail).

        DECLARE_LOCAL (verb);
        Init_Word(verb, Canon(SYM_CHANGE));
        Modify_String_Or_Binary(
            v,
            VAL_WORD_SPELLING(verb),
            temp,
            AM_PART,  // heed len for deletion
            len,
            1 // dup count
        );

        // Regardless of whether the whole string was reversed or just some
        // part from the index to the tail, the length shouldn't change.
        //
        assert(VAL_LEN_HEAD(v) == val_len_head);
        UNUSED(val_len_head);
    }
}


//
//  find_string: C
//
// Service routine for the FIND native on an ANY-STRING!
//
REBCNT find_string(
    REBCNT *len,
    REBSTR *str,
    REBCNT index,
    REBCNT end,
    const RELVAL *pattern,
    REBCNT flags,
    REBINT skip
){
    // !!! `index` and `end` integrate the /PART.  If the /PART was negative,
    // then index would have been swapped to be the lower value...making what
    // was previously the index the limit.  However, that does not work with
    // negative `skip` values, which by default considers 0 the limit of the
    // backkwards search but otherwise presumably want a /PART to limit it.
    // Passing in a real "limit" vs. an end which could be greater or less
    // than the index would be one way of resolving this problem.  But it's
    // a missing feature for now to do FIND/SKIP/PART with a negative skip.
    //
    assert(end >= index);

    if (ANY_STRING(pattern)) {
        bool str_is_all_ascii = false;
        bool pattern_is_all_ascii = false;
        if (str_is_all_ascii and not pattern_is_all_ascii)
            return NOT_FOUND; // can't possibly find non-ascii in ascii

        // !!! A TAG! does not have its delimiters in it.  The logic of the
        // find would have to be rewritten to accomodate this, and it's a
        // bit tricky as it is.  Let it settle down before trying that--and
        // for now just form the tag into a temporary alternate series.

        REBSTR *formed = nullptr;
        REBYTE *bp2;
        REBSIZ size2;
        if (not IS_TEXT(pattern)) { // !!! for TAG!, but what about FILE! etc?
            formed = Copy_Form_Value(pattern, 0);
            *len = STR_LEN(formed);
            bp2 = STR_HEAD(formed);
            size2 = STR_SIZE(formed);
        }
        else {
            *len = VAL_LEN_AT(pattern);
            bp2 = VAL_STRING_AT(pattern);
            size2 = VAL_SIZE_LIMIT_AT(NULL, pattern, *len);
        }

        REBCNT result;

        if (
            str_is_all_ascii
            and skip == 1  // optimizations not written for skips
        ){
            if (*len > end - index)  // series not long enough for pattern
                return NOT_FOUND;

            assert(pattern_is_all_ascii);  // checked above

            // If one knew that the series being searched in was all ASCII,
            // then the same search code can be used as for binary...because
            // the binary offset can be used as a character position.

            if (flags & AM_FIND_CASE)
                result = Find_Bin_In_Bin(
                    SER(str),  // all_ascii can be treated same as binary
                    index,
                    bp2,
                    size2,
                    flags & AM_FIND_MATCH
                );
            else
                return Find_Str_In_Bin(
                    SER(str),  // all_ascii can be treated same as binary
                    index,
                    bp2,
                    *len,
                    size2,
                    flags & AM_FIND_MATCH
                );
        }
        else
            result = Find_Str_In_Str(
                str,  // not all_ascii, has multibyte utf-8 sequences
                index,
                end,
                skip,
                formed ? formed : VAL_STRING(pattern),
                formed ? 0 : VAL_INDEX(pattern),
                *len,
                flags & (AM_FIND_MATCH | AM_FIND_CASE)
            );

        if (formed)
            Free_Unmanaged_Series(SER(formed));

        return result;
    }
    else if (IS_CHAR(pattern)) {
        *len = 1;

        return Find_Char_In_Str(
            VAL_CHAR(pattern),
            str,
            index,
            end,
            skip,
            flags & (AM_FIND_MATCH | AM_FIND_CASE)
        );
    }
    else if (IS_BITSET(pattern)) {
        *len = 1;

        return Find_Str_Bitset(
            str,
            index,
            end,
            skip,
            VAL_BITSET(pattern),
            flags & (AM_FIND_MATCH | AM_FIND_CASE)
        );
    }
    else if (IS_BINARY(pattern)) {
        //
        // Finding an arbitrary binary (which may not be UTF-8) in a string
        // is probably most sensibly done by thinking of the string itself
        // as a binary, and then searching in that.  Invalid UTF-8 patterns
        // simply would not be found.
        //
        REBSIZ psize = VAL_LEN_AT(pattern);
        if (psize == 0)
            return index;  // empty binaries / strings are matches

        const REBYTE *pbytes = VAL_BIN_AT(pattern);
        REBSIZ offset = cast(REBYTE*, STR_AT(str, index)) - STR_HEAD(str);
        REBCNT result = Find_Bin_In_Bin(
            SER(str),
            offset,
            pbytes,
            psize,
            flags & (AM_FIND_MATCH)
        );
        if (result == NOT_FOUND)
            return NOT_FOUND;

        // We found a match, but the binary find did not count how many
        // codepoints that was.  But what we know now is that it was valid.
        // step through it to get the length.

        REBCHR(const*) cp = cast(REBCHR(const*), pbytes);
        REBCHR(const*) ep = cast(REBCHR(const*), pbytes + psize);

        REBUNI c;
        cp = NEXT_CHR(&c, cp);
        *len = 1;
        for (; cp != ep; cp = NEXT_CHR(&c, cp))
            ++(*len);
        return result;
    }
    else
        fail ("find_string() received unknown pattern datatype");
}


// Common behaviors for:
//
//     MAKE STRING! ...
//     TO STRING! ...
//
// !!! MAKE and TO were not historically very clearly differentiated in
// Rebol, and so often they would "just do the same thing".  Ren-C ultimately
// will seek to limit the synonyms/polymorphism, e.g. MAKE or TO STRING! of a
// STRING! acting as COPY, in favor of having the user call COPY explicilty.
//
// Note also the existence of AS should be able to reduce copying, e.g.
// `print ["spelling is" as string! word]` will be cheaper than TO or MAKE.
//
static REBSTR *MAKE_TO_String_Common(const REBVAL *arg)
{
    if (IS_BINARY(arg))
        return Make_Sized_String_UTF8(
            cs_cast(VAL_BIN_AT(arg)), VAL_LEN_AT(arg)
        );

    if (ANY_STRING(arg))
        return Copy_String_At(arg);

    if (ANY_WORD(arg))
        return Copy_Mold_Value(arg, MOLD_FLAG_0);

    if (IS_CHAR(arg))
        return Make_Codepoint_String(VAL_CHAR(arg));

    return Copy_Form_Value(arg, MOLD_FLAG_TIGHT);
}


//
//  MAKE_String: C
//
REB_R MAKE_String(
    REBVAL *out,
    enum Reb_Kind kind,
    const REBVAL* opt_parent,
    const REBVAL *def
){
    if (opt_parent)
        fail (Error_Bad_Make_Parent(kind, opt_parent));

    if (IS_INTEGER(def)) {
        //
        // !!! R3-Alpha tolerated decimal, e.g. `make text! 3.14`, which
        // is semantically nebulous (round up, down?) and generally bad.
        // Red continues this behavior.
        //
        return Init_Any_String(out, kind, Make_Unicode(Int32s(def, 0)));
    }

    if (IS_BLOCK(def)) {
        //
        // The construction syntax for making strings that are preloaded with
        // an offset into the data is #[string ["abcd" 2]].
        //
        // !!! In R3-Alpha make definitions didn't have to be a single value
        // (they are for compatibility between construction syntax and MAKE
        // in Ren-C).  So the positional syntax was #[string! "abcd" 2]...
        // while #[string ["abcd" 2]] would join the pieces together in order
        // to produce #{abcd2}.  That behavior is not available in Ren-C.

        if (VAL_ARRAY_LEN_AT(def) != 2)
            goto bad_make;

        RELVAL *first = VAL_ARRAY_AT(def);
        if (not ANY_STRING(first))
            goto bad_make;

        RELVAL *index = VAL_ARRAY_AT(def) + 1;
        if (!IS_INTEGER(index))
            goto bad_make;

        REBINT i = Int32(index) - 1 + VAL_INDEX(first);
        if (i < 0 || i > cast(REBINT, VAL_LEN_AT(first)))
            goto bad_make;

        return Init_Any_Series_At(out, kind, VAL_SERIES(first), i);
    }

    return Init_Any_String(out, kind, MAKE_TO_String_Common(def));

  bad_make:
    fail (Error_Bad_Make(kind, def));
}


//
//  TO_String: C
//
REB_R TO_String(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    return Init_Any_String(out, kind, MAKE_TO_String_Common(arg));
}


enum COMPARE_CHR_FLAGS {
    CC_FLAG_CASE = 1 << 0, // Case sensitive sort
    CC_FLAG_REVERSE = 1 << 1 // Reverse sort order
};


//
//  Compare_Chr: C
//
// This function is called by qsort_r, on behalf of the string sort
// function.  The `thunk` is an argument passed through from the caller
// and given to us by the sort routine, which tells us about the string
// and the kind of sort that was requested.
//
// !!! As of UTF-8 everywhere, this will only work on all-ASCII strings.
//
static int Compare_Chr(void *thunk, const void *v1, const void *v2)
{
    REBCNT * const flags = cast(REBCNT*, thunk);

    REBYTE b1 = *cast(const REBYTE*, v1);
    REBYTE b2 = *cast(const REBYTE*, v2);

    assert(b1 < 0x80 && b2 < 0x80);

    if (*flags & CC_FLAG_CASE) {
        if (*flags & CC_FLAG_REVERSE)
            return b2 - b1;
        else
            return b1 - b2;
    }
    else {
        if (*flags & CC_FLAG_REVERSE)
            return LO_CASE(b2) - LO_CASE(b1);
        else
            return LO_CASE(b1) - LO_CASE(b2);
    }
}


//
//  Sort_String: C
//
static void Sort_String(
    REBVAL *string,
    bool ccase,
    REBVAL *skipv,
    REBVAL *compv,
    REBVAL *part,
    bool rev
){
    // !!! System appears to boot without a sort of a string.  A different
    // method will be needed for UTF-8... qsort() cannot work with variable
    // sized codepoints.  However, it could work if all the codepoints were
    // known to be ASCII range in the memory of interest, maybe common case.

    if (not IS_BLANK(compv))
        fail (Error_Bad_Refine_Raw(compv)); // !!! didn't seem to be supported (?)

    REBCNT skip = 1;
    REBCNT size = 1;
    REBCNT thunk = 0;

    REBCNT len = Part_Len_May_Modify_Index(string, part);  // length of sort
    if (len <= 1)
        return;

    if (not IS_BLANK(skipv)) {
        skip = Get_Num_From_Arg(skipv);
        if (skip <= 0 || len % skip != 0 || skip > len)
            fail (skipv);
    }

    // Use fast quicksort library function:
    if (skip > 1) len /= skip, size *= skip;

    if (ccase) thunk |= CC_FLAG_CASE;
    if (rev) thunk |= CC_FLAG_REVERSE;

    reb_qsort_r(
        VAL_RAW_DATA_AT(string),
        len,
        size * SER_WIDE(VAL_SERIES(string)),
        &thunk,
        Compare_Chr
    );
}


//
//  PD_String: C
//
REB_R PD_String(
    REBPVS *pvs,
    const REBVAL *picker,
    const REBVAL *opt_setval
){
    REBSTR *s = VAL_STRING(pvs->out);

    // Note: There was some more careful management of overflow here in the
    // PICK and POKE actions, before unification.  But otherwise the code
    // was less thorough.  Consider integrating this bit, though it seems
    // that a more codebase-wide review should be given to the issue.
    //
    /*
        REBINT len = Get_Num_From_Arg(arg);
        if (
            REB_I32_SUB_OF(len, 1, &len)
            || REB_I32_ADD_OF(index, len, &index)
            || index < 0 || index >= tail
        ){
            fail (Error_Out_Of_Range(arg));
        }
    */

    if (opt_setval == NULL) { // PICK-ing
        if (IS_INTEGER(picker) or IS_DECIMAL(picker)) { // #2312
            REBINT n = Int32(picker);
            if (n == 0)
                return nullptr; // Rebol2/Red convention, 0 is bad pick
            if (n < 0)
                ++n; // Rebol2/Red convention, `pick tail "abc" -1` is #"c"
            n += VAL_INDEX(pvs->out) - 1;
            if (n < 0 or cast(REBCNT, n) >= STR_LEN(s))
                return nullptr;

            Init_Char_Unchecked(pvs->out, GET_CHAR_AT(s, n));
            return pvs->out;
        }

        if (not (IS_WORD(picker) or ANY_STRING(picker)))
            return R_UNHANDLED;

        // !!! This is a historical and questionable feature, where path
        // picking a string or word or otherwise out of a FILE! or URL! will
        // generate a new FILE! or URL! with a slash in it.
        //
        //     >> x: %foo
        //     >> type of 'x/bar
        //     == path!
        //
        //     >> x/bar
        //     == %foo/bar ;-- a FILE!
        //
        // This can only be done with evaluations, since FILE! and URL! have
        // slashes in their literal form:
        //
        //     >> type of '%foo/bar
        //     == file!
        //
        // Because Ren-C unified picking and pathing, this somewhat odd
        // feature is now part of PICKing a string from another string.

        DECLARE_MOLD (mo);
        Push_Mold(mo);

        Form_Value(mo, pvs->out);

        // This makes sure there's always a "/" at the end of the file before
        // appending new material via a picker:
        //
        //     >> x: %foo
        //     >> (x)/("bar")
        //     == %foo/bar
        //
        if (STR_SIZE(mo->series) - mo->offset == 0)
            Append_Codepoint(mo->series, '/');
        else {
            if (
                *SER_SEEK(REBYTE, SER(mo->series), STR_SIZE(mo->series) - 1)
                != '/'
            ){
                Append_Codepoint(mo->series, '/');
            }
        }

        // !!! Code here previously would handle this case:
        //
        //     >> x/("/bar")
        //     == %foo/bar
        //
        // It's changed, so now the way to do that would be to drop the last
        // codepoint in the mold buffer, or advance the index position of the
        // picker.  Punt on it for now, as it will be easier to write when
        // UTF-8 Everywhere is actually in effect.

        Form_Value(mo, picker);

        Init_Any_String(pvs->out, VAL_TYPE(pvs->out), Pop_Molded_String(mo));
        return pvs->out;
    }

    // Otherwise, POKE-ing

    FAIL_IF_READ_ONLY(pvs->out);

    if (not IS_INTEGER(picker))
        return R_UNHANDLED;

    REBINT n = Int32(picker);
    if (n == 0)
        fail (Error_Out_Of_Range(picker)); // Rebol2/Red convention for 0
    if (n < 0)
        ++n;
    n += VAL_INDEX(pvs->out) - 1;
    if (n < 0 or cast(REBCNT, n) >= STR_LEN(s))
        fail (Error_Out_Of_Range(picker));

    REBINT c;
    if (IS_CHAR(opt_setval)) {
        c = VAL_CHAR(opt_setval);
        if (c > cast(REBI64, MAX_UNI))
            return R_UNHANDLED;
    }
    else if (IS_INTEGER(opt_setval)) {
        c = Int32(opt_setval);
        if (c > cast(REBI64, MAX_UNI) || c < 0)
            return R_UNHANDLED;
    }
    else if (ANY_BINSTR(opt_setval)) {
        REBCNT i = VAL_INDEX(opt_setval);
        if (i >= VAL_LEN_HEAD(opt_setval))
            fail (opt_setval);

        c = GET_CHAR_AT(VAL_STRING(opt_setval), i);
    }
    else
        return R_UNHANDLED;

    SET_CHAR_AT(s, n, c);
    return R_INVISIBLE;
}


//
//  Form_Uni_Hex: C
//
// Fast var-length hex output for uni-chars.
// Returns next position (just past the insert).
//
REBYTE *Form_Uni_Hex(REBYTE *out, REBCNT n)
{
    REBYTE buffer[10];
    REBYTE *bp = &buffer[10];

    while (n != 0) {
        *(--bp) = Hex_Digits[n & 0xf];
        n >>= 4;
    }

    while (bp < &buffer[10])
        *out++ = *bp++;

    return out;
}


//
//  Mold_Uni_Char: C
//
// !!! These heuristics were used in R3-Alpha to decide when to output
// characters in strings as escape for molding.  It's not clear where to
// draw the line with it...should most printable characters just be emitted
// normally in the UTF-8 string with a few exceptions (like newline as ^/)?
//
// For now just preserve what was there, but do it as UTF8 bytes.
//
void Mold_Uni_Char(REB_MOLD *mo, REBUNI c, bool parened)
{
    REBSTR *buf = mo->series;

    // !!! The UTF-8 "Byte Order Mark" is an insidious thing which is not
    // necessary for UTF-8, not recommended by the Unicode standard, and
    // Rebol should not invisibly be throwing it out of strings or file reads:
    //
    // https://stackoverflow.com/q/2223882/
    //
    // But the codepoint (U+FEFF, byte sequence #{EF BB BF}) has no printable
    // representation.  So if it's going to be loaded as-is then it should
    // give some hint that it's there.
    //
    // !!! 0x1e is "record separator" which is handled specially too.  The
    // following rationale is suggested by @MarkI:
    //
    //     "Rebol special-cases RS because traditionally it is escape-^
    //      but Rebol uses ^ to indicate escaping so it has to do
    //      something else with that one."

    if (c >= 0x7F || c == 0x1E || c == 0xFEFF) {
        //
        // non ASCII, "^" (RS), or byte-order-mark must be ^(00) escaped.
        //
        // !!! Comment here said "do not AND with the above"
        //
        if (parened || c == 0x1E || c == 0xFEFF) {
            REBCNT len_old = STR_LEN(buf);
            REBSIZ size_old = STR_SIZE(buf);
            EXPAND_SERIES_TAIL(SER(buf), 7);  // worst case: ^(1234)
            TERM_STR_LEN_SIZE(buf, len_old, size_old);

            Append_Ascii(buf, "^\"");

            REBYTE *bp = BIN_TAIL(SER(buf));
            REBYTE *ep = Form_Uni_Hex(bp, c); // !!! Make a mold...
            TERM_STR_LEN_SIZE(
                buf,
                len_old + (ep - bp),
                STR_SIZE(buf) + (ep - bp)
            );
            Append_Codepoint(buf, ')');
            return;
        }

        Append_Codepoint(buf, c);
        return;
    }
    else if (not IS_CHR_ESC(c)) { // Spectre mitigation in MSVC w/o `not`
        Append_Codepoint(buf, c);
        return;
    }

    Append_Codepoint(buf, '^');
    Append_Codepoint(buf, Char_Escapes[c]);
}


//
//  Mold_Text_Series_At: C
//
void Mold_Text_Series_At(REB_MOLD *mo, REBSTR *s, REBCNT index) {
    REBSTR *buf = mo->series;

    if (index >= STR_LEN(s)) {
        Append_Ascii(buf, "\"\"");
        return;
    }

    REBCNT len = STR_LEN(s) - index;

    bool parened = GET_MOLD_FLAG(mo, MOLD_FLAG_NON_ANSI_PARENED);

    // Scan to find out what special chars the string contains?

    REBCNT escape = 0;      // escaped chars
    REBCNT brace_in = 0;    // {
    REBCNT brace_out = 0;   // }
    REBCNT newline = 0;     // lf
    REBCNT quote = 0;       // "
    REBCNT paren = 0;       // (1234)
    REBCNT chr1e = 0;
    REBCNT malign = 0;

    REBCHR(const*) up = STR_AT(s, index);

    REBCNT x;
    for (x = index; x < len; x++) {
        REBUNI c;
        up = NEXT_CHR(&c, up);

        switch (c) {
        case '{':
            brace_in++;
            break;

        case '}':
            brace_out++;
            if (brace_out > brace_in)
                malign++;
            break;

        case '"':
            quote++;
            break;

        case '\n':
            newline++;
            break;

        default:
            if (c == 0x1e)
                chr1e += 4; // special case of ^(1e)
            else if (IS_CHR_ESC(c))
                escape++;
            else if (c >= 0x1000)
                paren += 6; // ^(1234)
            else if (c >= 0x100)
                paren += 5; // ^(123)
            else if (c >= 0x80)
                paren += 4; // ^(12)
        }
    }

    if (brace_in != brace_out)
        malign++;

    if (NOT_MOLD_FLAG(mo, MOLD_FLAG_NON_ANSI_PARENED))
        paren = 0;

    up = STR_AT(s, index);

    // If it is a short quoted string, emit it as "string"
    //
    if (len <= MAX_QUOTED_STR && quote == 0 && newline < 3) {
        Append_Codepoint(buf, '"');

        REBCNT n;
        for (n = index; n < STR_LEN(s); n++) {
            REBUNI c;
            up = NEXT_CHR(&c, up);
            Mold_Uni_Char(mo, c, parened);
        }

        Append_Codepoint(buf, '"');
        return;
    }

    // It is a braced string, emit it as {string}:
    if (malign == 0)
        brace_in = brace_out = 0;

    Append_Codepoint(buf, '{');

    REBCNT n;
    for (n = index; n < STR_LEN(s); n++) {
        REBUNI c;
        up = NEXT_CHR(&c, up);

        switch (c) {
        case '{':
        case '}':
            if (malign) {
                Append_Codepoint(buf, '^');
                break;
            }
            // fall through
        case '\n':
        case '"':
            Append_Codepoint(buf, c);
            break;

        default:
            Mold_Uni_Char(mo, c, parened);
        }
    }

    Append_Codepoint(buf, '}');
}


// R3-Alpha's philosophy on URL! was:
//
// "Only alphanumerics [0-9a-zA-Z], the special characters $-_.+!*'(),
//  and reserved characters used for their reserved purposes may be used
//  unencoded within a URL."
//
// http://www.blooberry.com/indexdot/html/topics/urlencoding.htm
//
// Ren-C is working with a different model, where URL! is generic to custom
// schemes which may or may not follow the RFC for Internet URLs.  It also
// wishes to preserve round-trip copy-and-paste from URL bars in browsers
// to source and back.  Encoding concerns are handled elsewhere.
//
static void Mold_Url(REB_MOLD *mo, const REBCEL *v)
{
    Append_String(mo->series, v, VAL_LEN_AT(v));
}


static void Mold_File(REB_MOLD *mo, const REBCEL *v)
{
    REBCNT len = VAL_LEN_AT(v);

    Append_Codepoint(mo->series, '%');

    REBCHR(const*) cp = VAL_STRING_AT(v);

    REBCNT n;
    for (n = 0; n < len; ++n) {
        REBUNI c;
        cp = NEXT_CHR(&c, cp);

        if (IS_FILE_ESC(c))
            Form_Hex_Esc(mo, c); // c => %xx
        else
            Append_Codepoint(mo->series, c);
    }
}


static void Mold_Tag(REB_MOLD *mo, const REBCEL *v)
{
    Append_Codepoint(mo->series, '<');
    Append_String(mo->series, v, VAL_LEN_AT(v));
    Append_Codepoint(mo->series, '>');
}


//
//  MF_String: C
//
void MF_String(REB_MOLD *mo, const REBCEL *v, bool form)
{
    REBSTR *buf = mo->series;

    enum Reb_Kind kind = CELL_KIND(v); // may be literal reusing the cell
    assert(ANY_STRING_KIND(kind));

    // Special format for MOLD/ALL string series when not at head
    //
    if (GET_MOLD_FLAG(mo, MOLD_FLAG_ALL) && VAL_INDEX(v) != 0) {
        Pre_Mold(mo, v); // e.g. #[file! part
        Mold_Text_Series_At(mo, VAL_STRING(v), 0);
        Post_Mold(mo, v);
        return;
    }

    // The R3-Alpha forming logic was that every string type besides TAG!
    // would form with no delimiters, e.g. `form #foo` is just foo
    //
    if (form and kind != REB_TAG) {
        Append_String(buf, v, VAL_LEN_AT(v));
        return;
    }

    switch (kind) {
      case REB_TEXT:
        Mold_Text_Series_At(mo, VAL_STRING(v), VAL_INDEX(v));
        break;

      case REB_FILE:
        if (VAL_LEN_AT(v) == 0) {
            Append_Ascii(buf, "%\"\"");
            break;
        }
        Mold_File(mo, v);
        break;

      case REB_EMAIL:
      case REB_URL:
        Mold_Url(mo, v);
        break;

      case REB_TAG:
        Mold_Tag(mo, v);
        break;

      case REB_ISSUE:
        Append_Codepoint(mo->series, '#');
        Append_String(mo->series, v, VAL_LEN_AT(v));
        break;

      default:
        panic (v);
    }
}


//
//  REBTYPE: C
//
// Action handler for ANY-STRING!
//
REBTYPE(String)
{
    REBVAL *v = D_ARG(1);
    assert(ANY_STRING(v));

    REBVAL *arg = D_ARGC > 1 ? D_ARG(2) : NULL;

    REBCNT index = VAL_INDEX(v);
    REBCNT tail = VAL_LEN_HEAD(v);

    REBFLGS sop_flags;  // SOP_XXX "Set Operation" flags

    REBSYM sym = VAL_WORD_SYM(verb);
    switch (sym) {
        //
        // !!! INTERSECT, UNION, DIFFERENCE temporarily unavailable on STRING!
        //
      case SYM_REFLECT:
      case SYM_SKIP:
      case SYM_AT:
        return Series_Common_Action_Maybe_Unhandled(frame_, verb);

      case SYM_REMOVE: {
        INCLUDE_PARAMS_OF_REMOVE;

        UNUSED(PAR(series)); // already accounted for

        REBSTR *s = VAL_STRING(v);
        FAIL_IF_READ_ONLY(v);

        REBINT limit =  Part_Len_May_Modify_Index(v, ARG(part));
        if (index >= tail or limit == 0)
            RETURN (v);

        REBCNT len;
        REBSIZ size = VAL_SIZE_LIMIT_AT(&len, v, limit);

        REBSIZ offset = VAL_OFFSET_FOR_INDEX(v, index);
        REBSIZ size_old = STR_SIZE(s);

        Remove_Series_Units(SER(s), offset, size);  // should keep terminator
        Free_Bookmarks_Maybe_Null(s);
        SET_STR_LEN_SIZE(s, tail - len, size_old - size);  // no term needed

        RETURN (v); }

    //-- Modification:
      case SYM_APPEND:
      case SYM_INSERT:
      case SYM_CHANGE: {
        INCLUDE_PARAMS_OF_INSERT;

        UNUSED(PAR(series));
        UNUSED(PAR(value));

        UNUSED(REF(only)); // all strings appends are /ONLY...currently unused

        REBCNT len; // length of target
        if (VAL_WORD_SYM(verb) == SYM_CHANGE)
            len = Part_Len_May_Modify_Index(v, ARG(part));
        else
            len = Part_Len_Append_Insert_May_Modify_Index(arg, ARG(part));

        // Note that while inserting or removing NULL is a no-op, CHANGE with
        // a /PART can actually erase data.
        //
        if (IS_NULLED(arg) and len == 0) { // only nulls bypass write attempts
            if (sym == SYM_APPEND) // append always returns head
                VAL_INDEX(v) = 0;
            RETURN (v); // don't fail on read only if it would be a no-op
        }

        REBFLGS flags = 0;
        if (REF(part))
            flags |= AM_PART;
        if (REF(line))
            flags |= AM_LINE;

        VAL_INDEX(v) = Modify_String_Or_Binary(  // does read-only check
            v,
            VAL_WORD_SPELLING(verb),
            arg,
            flags,
            len,
            REF(dup) ? Int32(ARG(dup)) : 1
        );
        RETURN (v); }

    //-- Search:
    case SYM_SELECT:
    case SYM_FIND: {
        INCLUDE_PARAMS_OF_FIND;

        UNUSED(REF(reverse));  // Deprecated https://forum.rebol.info/t/1126
        UNUSED(REF(last));  // ...a HIJACK in %mezz-legacy errors if used

        UNUSED(PAR(series));
        UNUSED(PAR(pattern));

        // !!! R3-Alpha FIND/MATCH historically implied /TAIL.  Should it?
        //
        REBFLGS flags = (
            (REF(only) ? AM_FIND_ONLY : 0)
            | (REF(match) ? AM_FIND_MATCH : 0)
            | (REF(case) ? AM_FIND_CASE : 0)
        );

        if (REF(part))
            tail = Part_Tail_May_Modify_Index(v, ARG(part));

        REBINT skip;
        if (REF(skip)) {
            skip = VAL_INT32(ARG(skip));
            if (skip == 0)
                fail (PAR(skip));
        }
        else
            skip = 1;

        REBCNT len;
        REBCNT ret = find_string(
            &len, VAL_STRING(v), index, tail, arg, flags, skip
        );

        if (ret == NOT_FOUND)
            return nullptr;

        assert(ret <= cast(REBCNT, tail));

        if (sym == SYM_FIND) {
            if (REF(tail) or REF(match))
                ret += len;
            return Init_Any_Series_At(D_OUT, VAL_TYPE(v), VAL_SERIES(v), ret);
        }

        assert(sym == SYM_SELECT);

        ++ret;
        if (ret == tail)
            return nullptr;

        return Init_Char_Unchecked(
            D_OUT,
            CHR_CODE(STR_AT(VAL_STRING(v), ret))
        ); }

    case SYM_TAKE_P: {
        INCLUDE_PARAMS_OF_TAKE_P;

        FAIL_IF_READ_ONLY(v);

        UNUSED(PAR(series));

        if (REF(deep))
            fail (Error_Bad_Refines_Raw());

        REBCNT len;
        if (REF(part)) {
            len = Part_Len_May_Modify_Index(v, ARG(part));
            if (len == 0)
                return Init_Any_String(D_OUT, VAL_TYPE(v), Make_String(0));
        } else
            len = 1;

        // Note that /PART can change index

        if (REF(last)) {
            if (len > tail) {
                VAL_INDEX(v) = 0;
                len = tail;
            }
            else
                VAL_INDEX(v) = cast(REBCNT, tail - len);
        }

        if (VAL_INDEX(v) >= tail) {
            if (not REF(part))
                return nullptr;
            return Init_Any_String(D_OUT, VAL_TYPE(v), Make_String(0));
        }

        REBSTR *s = VAL_STRING(v);
        index = VAL_INDEX(v);

        // if no /PART, just return value, else return string
        //
        if (REF(part))
            Init_Any_String(D_OUT, VAL_TYPE(v), Copy_String_At_Limit(v, len));
        else
            Init_Char_Unchecked(D_OUT, CHR_CODE(VAL_STRING_AT(v)));

        Remove_Series_Len(SER(s), VAL_INDEX(v), len);
        return D_OUT; }

    case SYM_CLEAR: {
        FAIL_IF_READ_ONLY(v);
        REBSTR *s = VAL_STRING(v);

        if (index >= tail)
            RETURN (v);  // clearing after available data has no effect

        // !!! R3-Alpha would take this opportunity to make it so that if the
        // series is now empty, it reclaims the "bias" (unused capacity at
        // the head of the series).  One of many behaviors worth reviewing.
        //
        if (index == 0 and IS_SER_DYNAMIC(s))
            Unbias_Series(SER(s), false);

        Free_Bookmarks_Maybe_Null(s);  // review!
        REBSIZ offset = VAL_OFFSET_FOR_INDEX(v, index);
        Free_Bookmarks_Maybe_Null(s);

        TERM_STR_LEN_SIZE(s, cast(REBCNT, index), offset);
        RETURN (v); }

    //-- Creation:

    case SYM_COPY: {
        INCLUDE_PARAMS_OF_COPY;

        UNUSED(PAR(value));

        if (REF(deep) or REF(types))
            fail (Error_Bad_Refines_Raw());

        REBINT len = Part_Len_May_Modify_Index(v, ARG(part));

        return Init_Any_String(
            D_OUT,
            VAL_TYPE(v),
            Copy_String_At_Limit(v, len)
        ); }

    //-- Bitwise:

      case SYM_INTERSECT:
        sop_flags = SOP_FLAG_CHECK;
        goto set_operation;

      case SYM_UNION:
        sop_flags = SOP_FLAG_BOTH;
        goto set_operation;

      case SYM_DIFFERENCE:
        sop_flags = SOP_FLAG_BOTH | SOP_FLAG_CHECK | SOP_FLAG_INVERT;
        goto set_operation;

      set_operation: {

        INCLUDE_PARAMS_OF_DIFFERENCE;  // should all have same spec

        UNUSED(ARG(value1)); // covered by value

        return Init_Any_Series(
            D_OUT,
            VAL_TYPE(v),
            Make_Set_Operation_Series(
                v,
                ARG(value2),
                sop_flags,
                REF(case),
                REF(skip) ? Int32s(ARG(skip), 1) : 1
            )
        ); }

    //-- Special actions:

    case SYM_SWAP: {
        FAIL_IF_READ_ONLY(v);

        if (VAL_TYPE(v) != VAL_TYPE(arg))
            fail (Error_Not_Same_Type_Raw());

        FAIL_IF_READ_ONLY(arg);

        if (index < tail && VAL_INDEX(arg) < VAL_LEN_HEAD(arg))
            swap_chars(v, arg);
        RETURN (v); }

    case SYM_REVERSE: {
        INCLUDE_PARAMS_OF_REVERSE;
        UNUSED(ARG(series));

        FAIL_IF_READ_ONLY(v);

        REBINT len = Part_Len_May_Modify_Index(v, ARG(part));
        if (len > 0)
            reverse_string(v, len);
        RETURN (v); }

    case SYM_SORT: {
        INCLUDE_PARAMS_OF_SORT;

        FAIL_IF_READ_ONLY(v);

        UNUSED(PAR(series));

        if (REF(all))
            fail (Error_Bad_Refine_Raw(ARG(all)));

        if (not Is_String_Definitely_ASCII(v))
            fail ("UTF-8 Everywhere: String sorting temporarily unavailable");

        Sort_String(
            v,
            REF(case),
            ARG(skip),  // blank! if not /SKIP
            ARG(compare),  // blank! if not /COMPARE
            ARG(part),  // blank! if not /PART
            REF(reverse)
        );
        RETURN (v); }

    case SYM_RANDOM: {
        INCLUDE_PARAMS_OF_RANDOM;

        UNUSED(PAR(value));

        if (REF(seed)) { // string/binary contents are the seed
            assert(ANY_STRING(v));

            REBSIZ utf8_size;
            const REBYTE *utf8 = VAL_UTF8_AT(&utf8_size, v);
            Set_Random(Compute_CRC24(utf8, utf8_size));
            return Init_Void(D_OUT);
        }

        FAIL_IF_READ_ONLY(v);

        if (REF(only)) {
            if (index >= tail)
                return nullptr;
            index += cast(REBCNT, Random_Int(REF(secure))) % (tail - index);

            return Init_Char_Unchecked(
                D_OUT,
                GET_CHAR_AT(VAL_STRING(v), index)
            );
        }

        if (not Is_String_Definitely_ASCII(v))
            fail ("UTF-8 Everywhere: String shuffle temporarily unavailable");

        FAIL_IF_READ_ONLY(v);

        Shuffle_String(v, REF(secure));
        RETURN (v); }

    default:
        // Let the port system try the action, e.g. OPEN %foo.txt
        //
        if ((IS_FILE(v) or IS_URL(v)))
            return T_Port(frame_, verb);
    }

    return R_UNHANDLED;
}


//
//  Startup_String: C
//
void Startup_String(void)
{
    Char_Escapes = ALLOC_N_ZEROFILL(REBYTE, MAX_ESC_CHAR + 1);

    REBYTE *cp = Char_Escapes;
    REBYTE c;
    for (c = '@'; c <= '_'; c++)
        *cp++ = c;

    Char_Escapes[cast(REBYTE, '\t')] = '-'; // tab
    Char_Escapes[cast(REBYTE, '\n')] = '/'; // line feed
    Char_Escapes[cast(REBYTE, '"')] = '"';
    Char_Escapes[cast(REBYTE, '^')] = '^';

    URL_Escapes = ALLOC_N_ZEROFILL(REBYTE, MAX_URL_CHAR + 1);

    for (c = 0; c <= ' '; c++)
        URL_Escapes[c] = ESC_URL | ESC_FILE;

    const REBYTE *dc = cb_cast(";%\"()[]{}<>");

    for (c = LEN_BYTES(dc); c > 0; c--)
        URL_Escapes[*dc++] = ESC_URL | ESC_FILE;
}


//
//  Shutdown_String: C
//
void Shutdown_String(void)
{
    FREE_N(REBYTE, MAX_ESC_CHAR + 1, Char_Escapes);
    FREE_N(REBYTE, MAX_URL_CHAR + 1, URL_Escapes);
}
