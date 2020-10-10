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

#include "sys-core.h"

#include "sys-int-funcs.h"

#include "datatypes/sys-money.h"

#define MAX_QUOTED_STR  50  // max length of "string" before going to { }

REBYTE *Char_Escapes;
#define MAX_ESC_CHAR (0x60-1) // size of escape table
#define IS_CHR_ESC(c) ((c) <= MAX_ESC_CHAR and Char_Escapes[c])

REBYTE *URL_Escapes;
#define MAX_URL_CHAR (0x80-1)
#define IS_URL_ESC(c)  ((c) <= MAX_URL_CHAR and (URL_Escapes[c] & ESC_URL))
#define IS_FILE_ESC(c) ((c) <= MAX_URL_CHAR and (URL_Escapes[c] & ESC_FILE))

enum {
    ESC_URL = 1,
    ESC_FILE = 2,
    ESC_EMAIL = 4
};


//
//  CT_String: C
//
REBINT CT_String(REBCEL(const*) a, REBCEL(const*) b, bool strict)
{
    assert(ANY_STRING_KIND(CELL_TYPE(a)) or REB_ISSUE == CELL_TYPE(a));
    assert(ANY_STRING_KIND(CELL_TYPE(b)) or REB_ISSUE == CELL_TYPE(b));

    REBLEN l1;
    REBCHR(const*) cp1 = VAL_UTF8_LEN_SIZE_AT(&l1, nullptr, a);

    REBLEN l2;
    REBCHR(const*) cp2 = VAL_UTF8_LEN_SIZE_AT(&l2, nullptr, b);

    REBLEN len = MIN(l1, l2);

    for (; len > 0; len--) {
        REBUNI c1;
        REBUNI c2;

        cp1 = NEXT_CHR(&c1, cp1);
        cp2 = NEXT_CHR(&c2, cp2);

        REBINT d;
        if (strict)
            d = c1 - c2;
        else
            d = LO_CASE(c1) - LO_CASE(c2);

        if (d != 0)
            return d > 0 ? 1 : -1;
    }

    if (l1 == l2)
        return 0;

    return l1 > l2 ? 1 : -1;
}


/***********************************************************************
**
**  Local Utility Functions
**
***********************************************************************/


static void reverse_string(REBSTR *str, REBLEN index, REBLEN len)
{
    if (len == 0)
        return; // if non-zero, at least one character in the string

    if (Is_String_Definitely_ASCII(str)) {
        REBYTE *bp = STR_AT(str, index);

        REBLEN n = 0;
        REBLEN m = len - 1;
        for (; n < len / 2; n++, m--) {
            REBYTE b = bp[n];
            bp[n] = bp[m];
            bp[m] = b;
        }
    }
    else {
        // !!! This is an inefficient method for reversing strings with
        // variable size codepoints.  Better way could work in place:
        //
        // https://stackoverflow.com/q/199260/

        DECLARE_MOLD (mo);
        Push_Mold(mo);

        REBLEN val_len_head = STR_LEN(str);

        REBCHR(const*) up = STR_TAIL(str);  // last exists due to len != 0
        REBLEN n;
        for (n = 0; n < len; ++n) {
            REBUNI c;
            up = BACK_CHR(&c, up);
            Append_Codepoint(mo->series, c);
        }

        DECLARE_LOCAL (temp);
        Init_Text(temp, Pop_Molded_String(mo));

        // Effectively do a CHANGE/PART to overwrite the reversed portion of
        // the string (from the input value's index to the tail).

        DECLARE_LOCAL (string);  // !!! Temp value, string type is irrelevant
        Init_Any_String_At(string, REB_TEXT, str, index);
        Modify_String_Or_Binary(
            string,
            SYM_CHANGE,
            temp,
            AM_PART,  // heed len for deletion
            len,
            1 // dup count
        );

        // Regardless of whether the whole string was reversed or just some
        // part from the index to the tail, the length shouldn't change.
        //
        assert(VAL_LEN_HEAD(string) == val_len_head);
        UNUSED(val_len_head);
    }
}


//
//  find_string: C
//
// Service routine for the FIND native on an ANY-STRING!
//
REBLEN find_string(
    REBLEN *len,
    const REBSTR *str,
    REBLEN index,
    REBLEN end,
    REBCEL(const*) pattern,
    REBLEN flags,
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

    enum Reb_Kind type = CELL_TYPE(pattern);
    if (
        ANY_STRING_KIND(type)
        or ANY_WORD_KIND(type)
        or REB_INTEGER == type  // `find "ab10cd" 10` -> "10cd"
        or REB_ISSUE == type
    ){
        bool str_is_all_ascii = false;  // !!! future string flag feature
        bool pattern_is_all_ascii = false;
        if (str_is_all_ascii and not pattern_is_all_ascii)
            return NOT_FOUND;  // can't possibly find non-ascii in ascii

        // !!! A TAG! does not have its delimiters in it.  The logic of the
        // find would have to be rewritten to accomodate this, and it's a
        // bit tricky as it is.  Let it settle down before trying that--and
        // for now just form the tag into a temporary alternate series.

        REBSTR *formed = nullptr;
        REBCHR(const*) bp2;
        REBSIZ size2;
        if (
            CELL_TYPE(pattern) != REB_ISSUE
            and CELL_TYPE(pattern) != REB_TEXT
            and CELL_TYPE(pattern) != REB_WORD
         ){
            // !!! `<tag>`, `set-word:` but FILE!, etc?
            //
            formed = Copy_Form_Cell(pattern, 0);
            *len = STR_LEN(formed);
            bp2 = STR_HEAD(formed);
            size2 = STR_SIZE(formed);
        }
        else
            bp2 = VAL_UTF8_LEN_SIZE_AT(len, &size2, pattern);

        REBLEN result;

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
        else {
            DECLARE_LOCAL (temp);  // !!! Note: unmanaged
            if (formed) {
                RESET_CELL(temp, REB_TEXT, CELL_FLAG_FIRST_IS_NODE);
                PAYLOAD(Any, temp).first.node = NOD(formed);
                PAYLOAD(Any, temp).second.u = 0;  // index
            }

            result = Find_Str_In_Str(
                str,  // not all_ascii, has multibyte utf-8 sequences
                index,
                end,
                skip,
                formed ? temp : pattern,
                *len,
                flags & (AM_FIND_MATCH | AM_FIND_CASE)
            );
        }

        if (formed)
            Free_Unmanaged_Series(SER(formed));

        return result;
    }
    else if (type == REB_BITSET) {
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
    else if (type == REB_BINARY) {
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
        REBSIZ offset = STR_AT(str, index) - STR_HEAD(str);
        REBLEN result = Find_Bin_In_Bin(
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
static REBSTR *MAKE_TO_String_Common(
    const REBVAL *arg,
    enum Reb_Strmode strmode
){
    if (IS_BINARY(arg))
        return Append_UTF8_May_Fail(
            nullptr,
            cs_cast(VAL_BIN_AT(arg)),
            VAL_LEN_AT(arg),
            strmode
        );

    if (ANY_STRING(arg))
        return Copy_String_At(arg);

    if (ANY_WORD(arg))
        return Copy_Mold_Value(arg, MOLD_FLAG_0);

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
        // !!! We can't really know how many bytes to allocate for a certain
        // number of codepoints.  UTF-8 may take up to UNI_ENCODED_MAX bytes
        // (typically 4) per CHAR!.  For now we just assume the integer is
        // the expected *byte* capacity, not length, as we can do that.
        //
        // !!! R3-Alpha tolerated decimal, e.g. `make text! 3.14`, which
        // is semantically nebulous (round up, down?) and generally bad.
        // Red continues this behavior.
        //
        return Init_Any_String(out, kind, Make_String(Int32s(def, 0)));
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

        const RELVAL *first = VAL_ARRAY_AT(def);
        if (not ANY_STRING(first))
            goto bad_make;

        const RELVAL *index = VAL_ARRAY_AT(def) + 1;
        if (!IS_INTEGER(index))
            goto bad_make;

        REBINT i = Int32(index) - 1 + VAL_INDEX(first);
        if (i < 0 or i > cast(REBINT, VAL_LEN_AT(first)))
            goto bad_make;

        return Init_Any_Series_At(out, kind, VAL_SERIES(first), i);
    }

    return Init_Any_String(
        out,
        kind,
        MAKE_TO_String_Common(def, STRMODE_NO_CR)
    );

  bad_make:
    fail (Error_Bad_Make(kind, def));
}


//
//  TO_String: C
//
REB_R TO_String(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    if (kind == REB_ISSUE) {  // encompasses what would have been TO CHAR!
        if (IS_INTEGER(arg)) {
            //
            // `to issue! 1` is slated to keep the visual consistency intact,
            // so that you'd get #1 back.  With issue! and char! unified,
            // that means a way to get a codepoint is needed.  Since this is
            // more about capturing an internal implementation, that falls
            // under AS ISSUE!, which could handle multi-codepoint TUPLE! too.
            //
            fail ("Use AS ISSUE! to convert integer codepoint to ISSUE!");
        }
        if (IS_CHAR(arg) and VAL_CHAR(arg) == 0)
            fail (Error_Illegal_Zero_Byte_Raw());  // `#` acts as codepoint 0

        // Fall through
    }

    return Init_Any_String(
        out,
        kind,
        MAKE_TO_String_Common(arg, STRMODE_NO_CR)
    );
}


//
//  to-text: native [
//      {Variant of TO TEXT! with option to tolerate invisible codepoints}
//
//      return: [<opt> text!]
//      value [<blank> any-value!]
//      /relax "Allow invisible codepoints like CR when converting BINARY!"
//  ]
//
REBNATIVE(to_text)
{
    INCLUDE_PARAMS_OF_TO_TEXT;

    return Init_Any_String(
        D_OUT,
        REB_TEXT,
        MAKE_TO_String_Common(
            ARG(value),
            REF(relax) ? STRMODE_ALL_CODEPOINTS : STRMODE_NO_CR
        )
    );
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
    REBLEN * const flags = cast(REBLEN*, thunk);

    REBYTE b1 = *cast(const REBYTE*, v1);
    REBYTE b2 = *cast(const REBYTE*, v2);

    assert(b1 < 0x80 and b2 < 0x80);

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
//  PD_String: C
//
REB_R PD_String(
    REBPVS *pvs,
    const RELVAL *picker,
    const REBVAL *opt_setval
){
    // Note: There was some more careful management of overflow here in the
    // PICK and POKE actions, before unification.  But otherwise the code
    // was less thorough.  Consider integrating this bit, though it seems
    // that a more codebase-wide review should be given to the issue.
    //
    /*
        REBINT len = Get_Num_From_Arg(arg);
        if (
            REB_I32_SUB_OF(len, 1, &len)
            or REB_I32_ADD_OF(index, len, &index)
            or index < 0 or index >= tail
        ){
            fail (Error_Out_Of_Range(arg));
        }
    */

    if (opt_setval == NULL) { // PICK-ing
        const REBSTR *s = VAL_STRING(pvs->out);
        if (IS_INTEGER(picker) or IS_DECIMAL(picker)) { // #2312
            REBINT n = Int32(picker);
            if (n == 0)
                return nullptr; // Rebol2/Red convention, 0 is bad pick
            if (n < 0)
                ++n; // Rebol2/Red convention, `pick tail "abc" -1` is #"c"
            n += VAL_INDEX(pvs->out) - 1;
            if (n < 0 or cast(REBLEN, n) >= STR_LEN(s))
                return nullptr;

            Init_Char_Unchecked(pvs->out, GET_CHAR_AT(s, n));
            return pvs->out;
        }

        if (IS_BLANK(picker)) {
            //
            // `f: %foo | f/bar/` should work
            // https://github.com/rebol/rebol-issues/issues/2378
            //
            picker = EMPTY_TEXT;
        }
        else if (
            not (IS_WORD(picker) or IS_TUPLE(picker) or ANY_STRING(picker))
        ){
            return R_UNHANDLED;
        }

        // !!! This is a historical and questionable feature, where path
        // picking a string or word or otherwise out of a FILE! or URL! will
        // generate a new FILE! or URL! with a slash in it.
        //
        //     >> x: %foo
        //     >> type of 'x/bar
        //     == path!
        //
        //     >> x/bar
        //     == %foo/bar  ; a FILE!
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
                *SER_AT(REBYTE, SER(mo->series), STR_SIZE(mo->series) - 1)
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

    REBSTR *s = VAL_STRING_ENSURE_MUTABLE(pvs->out);

    if (not IS_INTEGER(picker))
        return R_UNHANDLED;

    REBINT n = Int32(picker);
    if (n == 0)
        fail (Error_Out_Of_Range(SPECIFIC(picker))); // Rebol2/Red convention
    if (n < 0)
        ++n;
    n += VAL_INDEX(pvs->out) - 1;
    if (n < 0 or cast(REBLEN, n) >= STR_LEN(s))
        fail (Error_Out_Of_Range(SPECIFIC(picker)));


    if (IS_CHAR(opt_setval)) {
        Move_Value(pvs->out, opt_setval);
    }
    else if (IS_INTEGER(opt_setval)) {
        Init_Char_May_Fail(pvs->out, Int32(opt_setval));
    }
    else {
        // !!! This used to allow setting to a string to set to the first
        // character of that string, but that seems random.  Splicing
        // strings might be useful, but inconsistent with BLOCK!s which
        // preserve the value.  NULL might be interesting for removing
        // things, but changing the length could be confusing.  BINARY!
        // converted to a CHAR! could also be arguably useful.  Review.
        //
        return R_UNHANDLED;
    }

    REBUNI c = VAL_CHAR(pvs->out);
    if (c == 0)
        fail (Error_Illegal_Zero_Byte_Raw());

    SET_CHAR_AT(s, n, c);
    return R_INVISIBLE;
}


//
//  Form_Uni_Hex: C
//
// Fast var-length hex output for uni-chars.
// Returns next position (just past the insert).
//
REBYTE *Form_Uni_Hex(REBYTE *out, REBLEN n)
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

    if (c >= 0x7F or c == 0x1E or c == 0xFEFF) {
        //
        // non ASCII, "^" (RS), or byte-order-mark must be ^(00) escaped.
        //
        // !!! Comment here said "do not AND with the above"
        //
        if (parened or c == 0x1E or c == 0xFEFF) {
            REBLEN len_old = STR_LEN(buf);
            REBSIZ size_old = STR_SIZE(buf);
            EXPAND_SERIES_TAIL(SER(buf), 7);  // worst case: ^(1234)
            TERM_STR_LEN_SIZE(buf, len_old, size_old);

            Append_Ascii(buf, "^(");

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
void Mold_Text_Series_At(REB_MOLD *mo, const REBSTR *s, REBLEN index) {
    REBSTR *buf = mo->series;

    if (index >= STR_LEN(s)) {
        Append_Ascii(buf, "\"\"");
        return;
    }

    REBLEN len = STR_LEN(s) - index;

    bool parened = GET_MOLD_FLAG(mo, MOLD_FLAG_NON_ANSI_PARENED);

    // Scan to find out what special chars the string contains?

    REBLEN escape = 0;      // escaped chars
    REBLEN brace_in = 0;    // {
    REBLEN brace_out = 0;   // }
    REBLEN newline = 0;     // lf
    REBLEN quote = 0;       // "
    REBLEN paren = 0;       // (1234)
    REBLEN chr1e = 0;
    REBLEN malign = 0;

    REBCHR(const*) up = STR_AT(s, index);

    REBLEN x;
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
    if (len <= MAX_QUOTED_STR and quote == 0 and newline < 3) {
        Append_Codepoint(buf, '"');

        REBLEN n;
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

    REBLEN n;
    for (n = index; n < STR_LEN(s); n++) {
        REBUNI c;
        up = NEXT_CHR(&c, up);

        switch (c) {
          case '{':
          case '}':
            if (malign)
                Append_Codepoint(buf, '^');
            Append_Codepoint(buf, c);
            break;

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
static void Mold_Url(REB_MOLD *mo, REBCEL(const*) v)
{
    Append_String(mo->series, v, VAL_LEN_AT(v));
}


static void Mold_File(REB_MOLD *mo, REBCEL(const*) v)
{

    Append_Codepoint(mo->series, '%');

    REBLEN len;
    REBCHR(const*) cp = VAL_UTF8_LEN_SIZE_AT(&len, nullptr, v);

    REBLEN n;
    for (n = 0; n < len; ++n) {
        REBUNI c;
        cp = NEXT_CHR(&c, cp);

        if (IS_FILE_ESC(c))
            Form_Hex_Esc(mo, c); // c => %xx
        else
            Append_Codepoint(mo->series, c);
    }
}


static void Mold_Tag(REB_MOLD *mo, REBCEL(const*) v)
{
    Append_Codepoint(mo->series, '<');
    Append_String(mo->series, v, VAL_LEN_AT(v));
    Append_Codepoint(mo->series, '>');
}


//
//  MF_String: C
//
void MF_String(REB_MOLD *mo, REBCEL(const*) v, bool form)
{
    REBSTR *buf = mo->series;

    enum Reb_Kind kind = CELL_KIND(v); // may be literal reusing the cell
    assert(ANY_STRING_KIND(kind));

    // Special format for MOLD/ALL string series when not at head
    //
    if (GET_MOLD_FLAG(mo, MOLD_FLAG_ALL) and VAL_INDEX(v) != 0) {
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

    REBSYM sym = VAL_WORD_SYM(verb);

    REBLEN index = VAL_INDEX(v);
    REBLEN tail = VAL_LEN_HEAD(v);

    REBFLGS sop_flags;  // SOP_XXX "Set Operation" flags

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

        REBSTR *s = VAL_STRING_ENSURE_MUTABLE(v);

        REBINT limit;
        if (REF(part))
            limit = Part_Len_May_Modify_Index(v, ARG(part));
        else
            limit = 1;
        if (index >= tail or limit == 0)
            RETURN (v);

        REBLEN len;
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

        REBLEN len; // length of target
        if (VAL_WORD_SYM(verb) == SYM_CHANGE)
            len = Part_Len_May_Modify_Index(v, ARG(part));
        else
            len = Part_Limit_Append_Insert(ARG(part));

        // Note that while inserting or appending NULL is a no-op, CHANGE with
        // a /PART can actually erase data.
        //
        if (IS_NULLED(ARG(value)) and len == 0) {  // only nulls bypass
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
            cast(enum Reb_Symbol, sym),
            ARG(value),
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

        REBLEN len;
        REBLEN ret = find_string(
            &len, VAL_STRING(v), index, tail, ARG(pattern), flags, skip
        );

        if (ret == NOT_FOUND)
            return nullptr;

        assert(ret <= cast(REBLEN, tail));

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

      case SYM_TAKE: {
        INCLUDE_PARAMS_OF_TAKE;

        ENSURE_MUTABLE(v);

        UNUSED(PAR(series));

        if (REF(deep))
            fail (Error_Bad_Refines_Raw());

        REBLEN len;
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
                VAL_INDEX(v) = cast(REBLEN, tail - len);
        }

        if (VAL_INDEX(v) >= tail) {
            if (not REF(part))
                return nullptr;
            return Init_Any_String(D_OUT, VAL_TYPE(v), Make_String(0));
        }

        index = VAL_INDEX(v);

        // if no /PART, just return value, else return string
        //
        if (REF(part))
            Init_Any_String(D_OUT, VAL_TYPE(v), Copy_String_At_Limit(v, len));
        else
            Init_Char_Unchecked(D_OUT, CHR_CODE(VAL_STRING_AT(v)));

        Remove_Any_Series_Len(v, VAL_INDEX(v), len);
        return D_OUT; }

      case SYM_CLEAR: {
        REBSTR *s = VAL_STRING_ENSURE_MUTABLE(v);

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

        TERM_STR_LEN_SIZE(s, cast(REBLEN, index), offset);
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
                did REF(case),
                REF(skip) ? Int32s(ARG(skip), 1) : 1
            )
        ); }

    //-- Special actions:

      case SYM_SWAP: {
        REBVAL *arg = D_ARG(2);

        if (VAL_TYPE(v) != VAL_TYPE(arg))
            fail (Error_Not_Same_Type_Raw());

        REBSTR *v_str = VAL_STRING_ENSURE_MUTABLE(v);
        REBSTR *arg_str = VAL_STRING_ENSURE_MUTABLE(arg);

        if (index < tail and VAL_INDEX(arg) < VAL_LEN_HEAD(arg)) {
            REBUNI v_c = GET_CHAR_AT(v_str, VAL_INDEX(v));
            REBUNI arg_c = GET_CHAR_AT(arg_str, VAL_INDEX(arg));

            SET_CHAR_AT(v_str, VAL_INDEX(v), arg_c);
            SET_CHAR_AT(arg_str, VAL_INDEX(arg), v_c);
        }
        RETURN (v); }

      case SYM_REVERSE: {
        INCLUDE_PARAMS_OF_REVERSE;
        UNUSED(ARG(series));

        REBSTR *str = VAL_STRING_ENSURE_MUTABLE(v);

        Move_Value(D_OUT, v);  // save before index adjustment
        REBINT len = Part_Len_May_Modify_Index(v, ARG(part));
        if (len > 0)
            reverse_string(str, VAL_INDEX(v), len);
        return D_OUT; }

      case SYM_SORT: {
        INCLUDE_PARAMS_OF_SORT;

        REBYTE *data_at = VAL_STRING_AT_ENSURE_MUTABLE(v);

        UNUSED(PAR(series));

        if (REF(all))
            fail (Error_Bad_Refine_Raw(ARG(all)));

        if (not Is_String_Definitely_ASCII(VAL_STRING(v)))
            fail ("UTF-8 Everywhere: String sorting temporarily unavailable");

        // !!! A different method will be needed for UTF-8... qsort() can't
        // work with variable sized codepoints.  However, it could work if all
        // the codepoints were known to be ASCII range in the memory of
        // interest, maybe common case.

        if (REF(compare))
            fail (Error_Bad_Refine_Raw(PAR(compare))); // !!! not in R3-Alpha

        Move_Value(D_OUT, v);  // before index modification
        REBLEN len = Part_Len_May_Modify_Index(v, ARG(part));
        if (len <= 1)
            return D_OUT;

        REBLEN skip;
        if (not REF(skip))
            skip = 1;
        else {
            skip = Get_Num_From_Arg(ARG(skip));
            if (skip <= 0 or len % skip != 0 or skip > len)
                fail (PAR(skip));
        }

        // Use fast quicksort library function:
        REBLEN size = 1;
        if (skip > 1) {
            len /= skip;
            size *= skip;
        }

        REBLEN thunk = 0;
        if (REF(case))
            thunk |= CC_FLAG_CASE;
        if (REF(reverse))
            thunk |= CC_FLAG_REVERSE;

        reb_qsort_r(
            data_at,
            len,
            size * sizeof(REBYTE),  // only ASCII for now
            &thunk,
            Compare_Chr
        );
        return D_OUT; }

      case SYM_RANDOM: {
        INCLUDE_PARAMS_OF_RANDOM;

        UNUSED(PAR(value));

        if (REF(seed)) { // string/binary contents are the seed
            assert(ANY_STRING(v));

            REBSIZ utf8_size;
            REBCHR(const*) utf8 = VAL_UTF8_SIZE_AT(&utf8_size, v);
            Set_Random(Compute_CRC24(utf8, utf8_size));
            return Init_Void(D_OUT);
        }

        if (REF(only)) {
            if (index >= tail)
                return nullptr;
            index += cast(REBLEN, Random_Int(did REF(secure)))
                % (tail - index);

            return Init_Char_Unchecked(
                D_OUT,
                GET_CHAR_AT(VAL_STRING(v), index)
            );
        }

        REBSTR *str = VAL_STRING_ENSURE_MUTABLE(v);

        if (not Is_String_Definitely_ASCII(str))
            fail ("UTF-8 Everywhere: String shuffle temporarily unavailable");

        bool secure = did REF(secure);

        REBLEN n;
        for (n = STR_LEN(str) - index; n > 1;) {
            REBLEN k = index + cast(REBLEN, Random_Int(secure)) % n;
            n--;
            REBUNI swap = GET_CHAR_AT(str, k);
            SET_CHAR_AT(str, k, GET_CHAR_AT(str, n + index));
            SET_CHAR_AT(str, n + index, swap);
        }
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
    Char_Escapes = TRY_ALLOC_N_ZEROFILL(REBYTE, MAX_ESC_CHAR + 1);

    REBYTE *cp = Char_Escapes;
    REBYTE c;
    for (c = '@'; c <= '_'; c++)
        *cp++ = c;

    Char_Escapes[cast(REBYTE, '\t')] = '-'; // tab
    Char_Escapes[cast(REBYTE, '\n')] = '/'; // line feed
    Char_Escapes[cast(REBYTE, '"')] = '"';
    Char_Escapes[cast(REBYTE, '^')] = '^';

    URL_Escapes = TRY_ALLOC_N_ZEROFILL(REBYTE, MAX_URL_CHAR + 1);

    for (c = 0; c <= ' '; c++)
        URL_Escapes[c] = ESC_URL | ESC_FILE;

    const REBYTE *dc = cb_cast(";%\"()[]{}<>");

    for (c = strsize(dc); c > 0; c--)
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
