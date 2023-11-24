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

#undef Byte  // sys-zlib.h defines it compatibly (unsigned char)
#include "sys-zlib.h"  // for crc32_z()

#include "sys-int-funcs.h"

#include "cells/cell-money.h"

#define MAX_QUOTED_STR  50  // max length of "string" before going to { }

Byte* Char_Escapes;
#define MAX_ESC_CHAR (0x60-1) // size of escape table
#define IS_CHR_ESC(c) ((c) <= MAX_ESC_CHAR and Char_Escapes[c])

Byte* URL_Escapes;
#define MAX_URL_CHAR (0x80-1)
#define IS_URL_ESC(c)  ((c) <= MAX_URL_CHAR and (URL_Escapes[c] & ESC_URL))
#define IS_FILE_ESC(c) ((c) <= MAX_URL_CHAR and (URL_Escapes[c] & ESC_FILE))

enum {
    ESC_URL = 1,
    ESC_FILE = 2,
    ESC_EMAIL = 4
};


//
//  String_At: C
//
// Note that we only ever create caches for strings that have had String_At()
// run on them.  So the more operations that avoid String_At(), the better!
// Using String_Head() and String_Tail() will give a Utf8(*) that can be used
// to iterate much faster, and most of the strings in the system might be able
// to get away with not having any bookmarks at all.
//
Utf8(*) String_At(const_if_c String* s, REBLEN at) {
    assert(s != g_mold.buffer);  // String_At() makes bookmarks, don't want!

    assert(at <= String_Len(s));

    if (Is_Definitely_Ascii(s)) {  // can't have any false positives
        assert(not LINK(Bookmarks, s));  // mutations must ensure this
        return cast(Utf8(*), cast(Byte*, String_Head(s)) + at);
    }

    Utf8(*) cp;  // can be used to calculate offset (relative to String_Head())
    REBLEN index;

    BookmarkList* book = nullptr;  // updated at end if not nulled out
    if (Is_String_NonSymbol(s))
        book = LINK(Bookmarks, s);

  #if DEBUG_SPORADICALLY_DROP_BOOKMARKS
    if (book and SPORADICALLY(100)) {
        Free_Bookmarks_Maybe_Null(s);
        book = nullptr;
    }
  #endif

    REBLEN len = String_Len(s);

  #if DEBUG_TRACE_BOOKMARKS
    BOOKMARK_TRACE("len %ld @ %ld ", len, at);
    BOOKMARK_TRACE("%s", bookmark ? "bookmarked" : "no bookmark");
  #endif

    if (at < len / 2) {
        if (len < sizeof(Cell)) {
            if (Is_String_NonSymbol(s))
                assert(
                    Get_Series_Flag(s, DYNAMIC)  // e.g. mold buffer
                    or not book  // mutations must ensure this
                );
            goto scan_from_head;  // good locality, avoid bookmark logic
        }
        if (not book and Is_String_NonSymbol(s)) {
            book = Alloc_BookmarkList();
            LINK(Bookmarks, m_cast(String*, s)) = book;
            goto scan_from_head;  // will fill in bookmark
        }
    }
    else {
        if (len < sizeof(Cell)) {
            if (Is_String_NonSymbol(s))
                assert(
                    not book  // mutations must ensure this usually but...
                    or Get_Series_Flag(s, DYNAMIC)  // !!! mold buffer?
                );
            goto scan_from_tail;  // good locality, avoid bookmark logic
        }
        if (not book and Is_String_NonSymbol(s)) {
            book = Alloc_BookmarkList();
            LINK(Bookmarks, m_cast(String*, s)) = book;
            goto scan_from_tail;  // will fill in bookmark
        }
    }

    // Theoretically, a large UTF-8 string could have multiple "bookmarks".
    // That would complicate this logic by having to decide which one was
    // closest to be using.  For simplicity we just use one right now to
    // track the last access--which speeds up the most common case of an
    // iteration.  Improve as time permits!
    //
    assert(not book or Series_Used(book) == 1);  // only one

  blockscope {
    REBLEN booked = book ? BMK_INDEX(book) : 0;

    // `at` is always positive.  `booked - at` may be negative, but if it
    // is positive and bigger than `at`, faster to seek from head.
    //
    if (cast(REBINT, at) < cast(REBINT, booked) - cast(REBINT, at)) {
        if (at < sizeof(Cell))
            book = nullptr;  // don't update bookmark for near head search
        goto scan_from_head;
    }

    // `len - at` is always positive.  `at - booked` may be negative, but if
    // it is positive and bigger than `len - at`, faster to seek from tail.
    //
    if (cast(REBINT, len - at) < cast(REBINT, at) - cast(REBINT, booked)) {
        if (len - at < sizeof(Cell))
            book = nullptr;  // don't update bookmark for near tail search
        goto scan_from_tail;
    }

    index = booked;
    if (book)
        cp = cast(Utf8(*), Series_Data(s) + BMK_OFFSET(book));
    else
        cp = cast(Utf8(*), Series_Data(s));
  }

    if (index > at) {
      #if DEBUG_TRACE_BOOKMARKS
        BOOKMARK_TRACE("backward scan %ld", index - at);
      #endif
        goto scan_backward;
    }

  #if DEBUG_TRACE_BOOKMARKS
    BOOKMARK_TRACE("forward scan %ld", at - index);
  #endif
    goto scan_forward;

  scan_from_head:
  #if DEBUG_TRACE_BOOKMARKS
    BOOKMARK_TRACE("scan from head");
  #endif
    cp = String_Head(s);
    index = 0;

  scan_forward:
    assert(index <= at);
    for (; index != at; ++index)
        cp = Skip_Codepoint(cp);

    if (not book)
        return cp;

    goto update_bookmark;

  scan_from_tail:
  #if DEBUG_TRACE_BOOKMARKS
    BOOKMARK_TRACE("scan from tail");
  #endif
    cp = String_Tail(s);
    index = len;

  scan_backward:
    assert(index >= at);
    for (; index != at; --index)
        cp = Step_Back_Codepoint(cp);

    if (not book) {
      #if DEBUG_TRACE_BOOKMARKS
        BOOKMARK_TRACE("not cached\n");
      #endif
        return cp;
    }

  update_bookmark:
  #if DEBUG_TRACE_BOOKMARKS
    BOOKMARK_TRACE("caching %ld\n", index);
  #endif
    BMK_INDEX(book) = index;
    BMK_OFFSET(book) = cp - String_Head(s);

  #if DEBUG_VERIFY_STR_AT
    Utf8(*) check_cp = String_Head(s);
    REBLEN check_index = 0;
    for (; check_index != at; ++check_index)
        check_cp = Skip_Codepoint(check_cp);
    assert(check_cp == cp);
  #endif

    return cp;
}


//
//  CT_String: C
//
REBINT CT_String(NoQuote(const Cell*) a, NoQuote(const Cell*) b, bool strict)
{
    assert(
        Any_String_Kind(Cell_Heart(a))
        or REB_ISSUE == Cell_Heart(a)
        or REB_URL == Cell_Heart(a)
    );
    assert(
        Any_String_Kind(Cell_Heart(b))
        or REB_ISSUE == Cell_Heart(b)
        or REB_URL == Cell_Heart(a)
    );

    REBLEN l1;
    Utf8(const*) cp1 = Cell_Utf8_Len_Size_At(&l1, nullptr, a);

    REBLEN l2;
    Utf8(const*) cp2 = Cell_Utf8_Len_Size_At(&l2, nullptr, b);

    REBLEN len = MIN(l1, l2);

    for (; len > 0; len--) {
        Codepoint c1;
        Codepoint c2;

        cp1 = Utf8_Next(&c1, cp1);
        cp2 = Utf8_Next(&c2, cp2);

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


static void reverse_string(String* str, REBLEN index, Length len)
{
    if (len == 0)
        return; // if non-zero, at least one character in the string

    if (Is_String_Definitely_ASCII(str)) {
        Byte* bp = String_At(str, index);

        REBLEN n = 0;
        REBLEN m = len - 1;
        for (; n < len / 2; n++, m--) {
            Byte b = bp[n];
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

        Length len_head = String_Len(str);

        Utf8(const*) utf8 = String_Tail(str);  // last exists due to len != 0
        Count n;
        for (n = 0; n < len; ++n) {
            Codepoint c;
            utf8 = Utf8_Back(&c, utf8);
            Append_Codepoint(mo->series, c);
        }

        DECLARE_STABLE (temp);
        Init_Text(temp, Pop_Molded_String(mo));

        // Effectively do a CHANGE/PART to overwrite the reversed portion of
        // the string (from the input value's index to the tail).

        DECLARE_STABLE (string);  // !!! Temp value, string type is irrelevant
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
        assert(Cell_Series_Len_Head(string) == len_head);
        UNUSED(len_head);
    }
}


//
//  MAKE_String: C
//
Bounce MAKE_String(
    Level* level_,
    enum Reb_Kind kind,
    Option(Value(const*)) parent,
    const REBVAL *def
){
    if (parent)
        return RAISE(Error_Bad_Make_Parent(kind, unwrap(parent)));

    if (Is_Integer(def)) {  // new string with given integer capacity
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
        return Init_Any_String(OUT, kind, Make_String(Int32s(def, 0)));
    }

    if (Any_Utf8(def)) {  // new type for the UTF-8 data with new allocation
        Length len;
        Size size;
        const Byte* utf8 = Cell_Utf8_Len_Size_At(&len, &size, def);
        UNUSED(len);  // !!! Data already valid and checked, should leverage
        return Init_Any_String(
            OUT,
            kind,
            Append_UTF8_May_Fail(  // !!! Should never fail
                nullptr,
                cs_cast(utf8),
                size,
                STRMODE_ALL_CODEPOINTS
            )
        );
    }

    if (Is_Binary(def)) {  // not necessarily valid UTF-8, so must check
        Size size;
        const Byte* at = Cell_Binary_Size_At(&size, def);
        return Init_Any_String(
            OUT,
            kind,
            Append_UTF8_May_Fail(nullptr, cs_cast(at), size, STRMODE_NO_CR)
        );
    }

    if (Is_Block(def)) {
        //
        // The construction syntax for making strings that are preloaded with
        // an offset into the data is #[string ["abcd" 2]].
        //
        // !!! In R3-Alpha make definitions didn't have to be a single value
        // (they are for compatibility between construction syntax and MAKE
        // in Ren-C).  So the positional syntax was #[string! "abcd" 2]...
        // while #[string ["abcd" 2]] would join the pieces together in order
        // to produce #{abcd2}.  That behavior is not available in Ren-C.

        REBLEN len;
        const Cell* first = Cell_Array_Len_At(&len, def);

        if (len != 2)
            goto bad_make;

        if (not Any_String(first))
            goto bad_make;

        const Cell* index = first + 1;
        if (!Is_Integer(index))
            goto bad_make;

        REBINT i = Int32(index) - 1 + VAL_INDEX(first);
        if (i < 0 or i > cast(REBINT, Cell_Series_Len_At(first)))
            goto bad_make;

        return Init_Series_Cell_At(OUT, kind, Cell_Series(first), i);
    }

  bad_make:

    return RAISE(Error_Bad_Make(kind, def));
}


//
//  TO_String: C
//
Bounce TO_String(Level* level_, enum Reb_Kind kind, const REBVAL *arg)
{
    if (kind == REB_ISSUE) {  // encompasses what would have been TO CHAR!
        if (Is_Integer(arg)) {
            //
            // `to issue! 1` is slated to keep the visual consistency intact,
            // so that you'd get #1 back.  With issue! and char! unified,
            // that means a way to get a codepoint is needed.
            //
            return RAISE("Use CODEPOINT-TO-CHAR for codepoint to ISSUE!");
        }
        if (IS_CHAR(arg) and Cell_Codepoint(arg) == 0)
            return RAISE(Error_Illegal_Zero_Byte_Raw());  // `#` as codepoint 0

        // Fall through
    }

    if (Is_Binary(arg)) {
        //
        // !!! Historically TO would convert binaries to strings.  But as
        // the definition of TO has been questioned and evolving, that no
        // longer seems to make sense (e.g. if `TO TEXT! 1` is "1", the
        // concept of implementation transformations doesn't fit).  Keep
        // compatible for right now, but ultimately MAKE or AS should be
        // used for this.
        //
        Size size;
        const Byte* at = Cell_Binary_Size_At(&size, arg);
        return Init_Any_String(
            OUT,
            kind,
            Append_UTF8_May_Fail(nullptr, cs_cast(at), size, STRMODE_NO_CR)
        );
    }

    // !!! Historical behavior for TO TEXT! of TAG! did not FORM:
    //
    //     >> to text! <abc>
    //     == "abc"
    //
    // However, that behavior is likely to change, as this behavior should
    // be covered by `make text!` or `copy as text!`.  For the present
    // moment, it is kept as-is to avoid disruption.
    //
    if (Is_Tag(arg))
        return MAKE_String(level_, kind, nullptr, arg);

    return Init_Any_String(
        OUT,
        kind,
        Copy_Form_Value(arg, MOLD_FLAG_TIGHT)
    );
}


//
//  to-text: native [
//      {Variant of TO TEXT! with option to tolerate invisible codepoints}
//
//      return: [<opt> text!]
//      value [<maybe> any-value!]
//      /relax "Allow invisible codepoints like CR when converting BINARY!"
//  ]
//
DECLARE_NATIVE(to_text)
{
    INCLUDE_PARAMS_OF_TO_TEXT;

    if (Is_Binary(ARG(value)) and REF(relax)) {
        Size size;
        const Byte* at = Cell_Binary_Size_At(&size, ARG(value));
        return Init_Any_String(
            OUT,
            REB_TEXT,
            Append_UTF8_May_Fail(
                nullptr,
                cs_cast(at),
                size,
                STRMODE_ALL_CODEPOINTS
            )
        );
    }

    return rebValue("to text! @", ARG(value));
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

    Byte b1 = *c_cast(Byte*, v1);
    Byte b2 = *c_cast(Byte*, v2);

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
//  Form_Uni_Hex: C
//
// Fast var-length hex output for uni-chars.
// Returns next position (just past the insert).
//
Byte* Form_Uni_Hex(Byte* out, REBLEN n)
{
    Byte buffer[10];
    Byte* bp = &buffer[10];

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
void Mold_Uni_Char(REB_MOLD *mo, Codepoint c, bool parened)
{
    String* buf = mo->series;

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
            Append_Ascii(buf, "^(");

            Length len_old = String_Len(buf);
            Size size_old = String_Size(buf);
            Expand_Series_Tail(buf, 5);  // worst case: ^(1234), ^( is done
            Term_String_Len_Size(buf, len_old, size_old);

            Byte* bp = Binary_Tail(buf);
            Byte* ep = Form_Uni_Hex(bp, c); // !!! Make a mold...
            Term_String_Len_Size(
                buf,
                len_old + (ep - bp),
                size_old + (ep - bp)
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
void Mold_Text_Series_At(REB_MOLD *mo, const String* s, REBLEN index) {
    String* buf = mo->series;

    if (index >= String_Len(s)) {
        Append_Ascii(buf, "\"\"");
        return;
    }

    Length len = String_Len(s) - index;

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

    Utf8(const*) up = String_At(s, index);

    REBLEN x;
    for (x = index; x < len; x++) {
        Codepoint c;
        up = Utf8_Next(&c, up);

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

    up = String_At(s, index);

    // If it is a short quoted string, emit it as "string"
    //
    if (len <= MAX_QUOTED_STR and quote == 0 and newline < 3) {
        Append_Codepoint(buf, '"');

        REBLEN n;
        for (n = index; n < String_Len(s); n++) {
            Codepoint c;
            up = Utf8_Next(&c, up);
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
    for (n = index; n < String_Len(s); n++) {
        Codepoint c;
        up = Utf8_Next(&c, up);

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

    USED(escape);
    USED(paren);
    USED(chr1e);
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
static void Mold_Url(REB_MOLD *mo, NoQuote(const Cell*) v)
{
    Append_String(mo->series, v);
}


static void Mold_File(REB_MOLD *mo, NoQuote(const Cell*) v)
{

    Append_Codepoint(mo->series, '%');

    REBLEN len;
    Utf8(const*) cp = Cell_Utf8_Len_Size_At(&len, nullptr, v);

    REBLEN n;
    for (n = 0; n < len; ++n) {
        Codepoint c;
        cp = Utf8_Next(&c, cp);

        if (IS_FILE_ESC(c))
            Form_Hex_Esc(mo, c); // c => %xx
        else
            Append_Codepoint(mo->series, c);
    }
}


static void Mold_Tag(REB_MOLD *mo, NoQuote(const Cell*) v)
{
    Append_Codepoint(mo->series, '<');
    Append_String(mo->series, v);
    Append_Codepoint(mo->series, '>');
}


//
//  MF_String: C
//
void MF_String(REB_MOLD *mo, NoQuote(const Cell*) v, bool form)
{
    String* buf = mo->series;

    assert(Any_Stringlike(v));

    enum Reb_Kind kind = Cell_Heart(v);

    // Special format for MOLD/ALL string series when not at head
    //
    if (GET_MOLD_FLAG(mo, MOLD_FLAG_ALL) and VAL_INDEX(v) != 0) {
        Pre_Mold(mo, v); // e.g. #[file! part
        Mold_Text_Series_At(mo, Cell_String(v), 0);
        Post_Mold(mo, v);
        return;
    }

    // The R3-Alpha forming logic was that every string type besides TAG!
    // would form with no delimiters, e.g. `form #foo` is just foo
    //
    if (form and kind != REB_TAG) {
        Append_String(buf, v);
        return;
    }

    switch (kind) {
      case REB_TEXT:
        Mold_Text_Series_At(mo, Cell_String(v), VAL_INDEX(v));
        break;

      case REB_FILE:
        if (Cell_Series_Len_At(v) == 0) {
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
//  Did_Get_Series_Index_From_Picker: C
//
// Will fail if the picker is outright invalid, but return false if it should
// be NULL on the last step of a PICK.
//
bool Did_Get_Series_Index_From_Picker(
    REBINT *out,
    const REBVAL *v,
    const Cell* picker
){
    if (not (Is_Integer(picker) or Is_Decimal(picker)))  // !!! why DECIMAL! ?
        fail (Error_Bad_Pick_Raw(picker));

    REBINT n = Int32(picker);
    if (n == 0)
        return false;  // Rebol2 and Red pick of 0 is none

    if (n < 0)
        ++n;

    n += VAL_INDEX(v) - 1;

    if (n < 0 or cast(REBLEN, n) >= Cell_Series_Len_Head(v))
        return false;  // out of range, null unless POKE or more PICK-ing

    *out = n;
    return true;
}


//
//  REBTYPE: C
//
// Action handler for ANY-STRING!
//
REBTYPE(String)
{
    REBVAL *v = D_ARG(1);
    assert(Any_String(v));

    Option(SymId) id = Symbol_Id(verb);

    switch (id) {

    //=//// PICK* (see %sys-pick.h for explanation) ////////////////////////=//

      case SYM_PICK_P: {
        INCLUDE_PARAMS_OF_PICK_P;
        UNUSED(ARG(location));

        const Cell* picker = ARG(picker);
        REBINT n;
        if (not Did_Get_Series_Index_From_Picker(&n, v, picker))
            return nullptr;

        Codepoint c = Get_Char_At(Cell_String(v), n);

        return Init_Char_Unchecked(OUT, c); }


    //=//// POKE* (see %sys-pick.h for explanation) ////////////////////////=//

      case SYM_POKE_P: {
        INCLUDE_PARAMS_OF_POKE_P;
        UNUSED(ARG(location));

        const Cell* picker = ARG(picker);
        REBINT n;
        if (not Did_Get_Series_Index_From_Picker(&n, v, picker))
            fail (Error_Out_Of_Range(picker));

        REBVAL *setval = ARG(value);

        Codepoint c;
        if (IS_CHAR(setval)) {
            c = Cell_Codepoint(setval);
        }
        else if (Is_Integer(setval)) {
            c = Int32(setval);
        }
        else  // CHANGE is a better route for splicing/removal/etc.
            fail (PARAM(value));

        if (c == 0)
            fail (Error_Illegal_Zero_Byte_Raw());

        String* s = Cell_String_Ensure_Mutable(v);
        Set_Char_At(s, n, c);

        return nullptr; }  // Array* is still fine, caller need not update


      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;
        UNUSED(ARG(value));  // accounted for by `v`

        if (VAL_WORD_ID(ARG(property)) == SYM_SIZE) {
            Size size;
            Cell_Utf8_Size_At(&size, v);
            return Init_Integer(OUT, size);
        }
        return Series_Common_Action_Maybe_Unhandled(level_, verb); }

      case SYM_UNIQUE:
      case SYM_INTERSECT:
      case SYM_UNION:
      case SYM_DIFFERENCE:
      case SYM_EXCLUDE:
        //
      case SYM_SKIP:
      case SYM_AT:
        return Series_Common_Action_Maybe_Unhandled(level_, verb);

      case SYM_REMOVE: {
        INCLUDE_PARAMS_OF_REMOVE;

        UNUSED(PARAM(series)); // already accounted for

        String* s = Cell_String_Ensure_Mutable(v);

        REBINT limit;
        if (REF(part))
            limit = Part_Len_May_Modify_Index(v, ARG(part));
        else
            limit = 1;

        REBLEN index = VAL_INDEX(v);  // Part calculation may have changed!
        REBLEN tail = Cell_Series_Len_Head(v);

        if (index >= tail or limit == 0)
            return COPY(v);

        Length len;
        Size size = Cell_String_Size_Limit_At(&len, v, limit);

        Size offset = VAL_BYTEOFFSET_FOR_INDEX(v, index);
        Size size_old = String_Size(s);

        Remove_Series_Units(s, offset, size);  // !!! at one time, kept term
        Free_Bookmarks_Maybe_Null(s);
        Term_String_Len_Size(s, tail - len, size_old - size);

        return COPY(v); }

    //-- Modification:
      case SYM_APPEND:
      case SYM_INSERT:
      case SYM_CHANGE: {
        INCLUDE_PARAMS_OF_INSERT;
        UNUSED(PARAM(series));

        Value(*) arg = ARG(value);
        assert(not Is_Nulled(arg));  // not an <opt> parameter

        REBLEN len; // length of target
        if (Symbol_Id(verb) == SYM_CHANGE)
            len = Part_Len_May_Modify_Index(v, ARG(part));
        else
            len = Part_Limit_Append_Insert(ARG(part));

        // Note that while inserting or appending NULL is a no-op, CHANGE with
        // a /PART can actually erase data.
        //
        if (Is_Void(arg) and len == 0) {
            if (id == SYM_APPEND) // append always returns head
                VAL_INDEX_RAW(v) = 0;
            return COPY(v);  // don't fail on read only if would be a no-op
        }

        Flags flags = 0;
        if (REF(part))
            flags |= AM_PART;
        if (REF(line))
            flags |= AM_LINE;

        // !!! This mimics historical type tolerance, e.g. not everything that
        // gets appended has to be a string:
        //
        //     rebol2>> append "abc" 'd
        //     == "abcd"
        //
        // However it will not try to FORM blocks or other arrays; it only
        // accepts isotopic blocks to imply "append each item individually".
        //
        if (Is_Void(arg)) {
            // not necessarily a no-op (e.g. CHANGE can erase)
        }
        else if (Is_Splice(arg)) {
            QUOTE_BYTE(arg) = UNQUOTED_1;
        }
        else if (Is_Isotope(arg)) {  // only SPLICE! in typecheck
            fail (Error_Bad_Isotope(arg));  // ...but that doesn't filter yet
        }
        else if (Any_Array(arg))
            fail (ARG(value));  // error on `append "abc" [d e]` w/o SPREAD

        VAL_INDEX_RAW(v) = Modify_String_Or_Binary(  // does read-only check
            v,
            unwrap(id),
            arg,
            flags,
            len,
            REF(dup) ? Int32(ARG(dup)) : 1
        );
        return COPY(v); }

    //-- Search:
      case SYM_SELECT:
      case SYM_FIND: {
        INCLUDE_PARAMS_OF_FIND;
        if (Is_Isotope(ARG(pattern)))
            fail (ARG(pattern));

        UNUSED(PARAM(series));

        Flags flags = (
            (REF(match) ? AM_FIND_MATCH : 0)
            | (REF(case) ? AM_FIND_CASE : 0)
        );

        REBLEN tail = Part_Tail_May_Modify_Index(v, ARG(part));

        REBINT skip;
        if (REF(skip)) {
            skip = VAL_INT32(ARG(skip));
            if (skip == 0)
                fail (PARAM(skip));
        }
        else
            skip = 1;

        REBLEN len;
        REBINT find = Find_Value_In_Binstr(
            &len, v, tail, ARG(pattern), flags, skip
        );

        if (find == NOT_FOUND)
            return nullptr;  // don't Proxy_Multi_Returns

        REBLEN ret = cast(REBLEN, find);
        assert(ret <= cast(REBLEN, tail));
        UNUSED(find);

        if (id == SYM_FIND) {
            Init_Series_Cell_At(
                ARG(tail),
                VAL_TYPE(v),
                Cell_Series(v),
                ret + len
            );
            Init_Series_Cell_At(
                OUT,
                VAL_TYPE(v),
                Cell_Series(v),
                ret
            );
            return Proxy_Multi_Returns(level_);
        }

        assert(id == SYM_SELECT);

        ++ret;
        if (ret == tail)
            return nullptr;

        return Init_Char_Unchecked(
            OUT,
            Codepoint_At(String_At(Cell_String(v), ret))
        ); }

      case SYM_TAKE: {
        INCLUDE_PARAMS_OF_TAKE;

        Ensure_Mutable(v);

        UNUSED(PARAM(series));

        if (REF(deep))
            fail (Error_Bad_Refines_Raw());

        REBLEN len;
        if (REF(part)) {
            len = Part_Len_May_Modify_Index(v, ARG(part));
            if (len == 0)
                return Init_Any_String(OUT, VAL_TYPE(v), Make_String(0));
        } else
            len = 1;

        // Note that /PART can change index

        REBLEN tail = Cell_Series_Len_Head(v);

        if (REF(last)) {
            if (len > tail) {
                VAL_INDEX_RAW(v) = 0;
                len = tail;
            }
            else
                VAL_INDEX_RAW(v) = cast(REBLEN, tail - len);
        }

        if (VAL_INDEX(v) >= tail) {
            if (not REF(part))
                return RAISE(Error_Nothing_To_Take_Raw());
            return Init_Any_String(OUT, VAL_TYPE(v), Make_String(0));
        }

        // if no /PART, just return value, else return string
        //
        if (REF(part))
            Init_Any_String(OUT, VAL_TYPE(v), Copy_String_At_Limit(v, len));
        else
            Init_Char_Unchecked(OUT, Codepoint_At(Cell_String_At(v)));

        Remove_Any_Series_Len(v, VAL_INDEX(v), len);
        return OUT; }

      case SYM_CLEAR: {
        String* s = Cell_String_Ensure_Mutable(v);

        REBLEN index = VAL_INDEX(v);
        REBLEN tail = Cell_Series_Len_Head(v);

        if (index >= tail)
            return COPY(v);  // clearing after available data has no effect

        // !!! R3-Alpha would take this opportunity to make it so that if the
        // series is now empty, it reclaims the "bias" (unused capacity at
        // the head of the series).  One of many behaviors worth reviewing.
        //
        if (index == 0 and Get_Series_Flag(s, DYNAMIC))
            Unbias_Series(s, false);

        Free_Bookmarks_Maybe_Null(s);  // review!
        Size offset = VAL_BYTEOFFSET_FOR_INDEX(v, index);
        Free_Bookmarks_Maybe_Null(s);

        Term_String_Len_Size(s, cast(REBLEN, index), offset);
        return COPY(v); }

    //-- Creation:

      case SYM_COPY: {
        INCLUDE_PARAMS_OF_COPY;

        UNUSED(PARAM(value));
        UNUSED(REF(deep));  // /DEEP is historically ignored on ANY-STRING!

        REBINT len = Part_Len_May_Modify_Index(v, ARG(part));

        return Init_Any_String(
            OUT,
            VAL_TYPE(v),
            Copy_String_At_Limit(v, len)
        ); }

    //-- Special actions:

      case SYM_SWAP: {
        REBVAL *arg = D_ARG(2);

        if (VAL_TYPE(v) != VAL_TYPE(arg))
            fail (Error_Not_Same_Type_Raw());

        String* v_str = Cell_String_Ensure_Mutable(v);
        String* arg_str = Cell_String_Ensure_Mutable(arg);

        REBLEN index = VAL_INDEX(v);
        REBLEN tail = Cell_Series_Len_Head(v);

        if (index < tail and VAL_INDEX(arg) < Cell_Series_Len_Head(arg)) {
            Codepoint v_c = Get_Char_At(v_str, VAL_INDEX(v));
            Codepoint arg_c = Get_Char_At(arg_str, VAL_INDEX(arg));

            Set_Char_At(v_str, VAL_INDEX(v), arg_c);
            Set_Char_At(arg_str, VAL_INDEX(arg), v_c);
        }
        return COPY(v); }

      case SYM_REVERSE: {
        INCLUDE_PARAMS_OF_REVERSE;
        UNUSED(ARG(series));

        String* str = Cell_String_Ensure_Mutable(v);

        Copy_Cell(OUT, v);  // save before index adjustment
        REBINT len = Part_Len_May_Modify_Index(v, ARG(part));
        if (len > 0)
            reverse_string(str, VAL_INDEX(v), len);
        return OUT; }

      case SYM_SORT: {
        INCLUDE_PARAMS_OF_SORT;

        UNUSED(PARAM(series));

        String* str = Cell_String_Ensure_Mutable(v);  // just ensure mutability
        UNUSED(str);  // we use the Cell_Utf8_At() accessor, which is const

        if (REF(all))
            fail (Error_Bad_Refines_Raw());

        if (REF(compare))
            fail (Error_Bad_Refines_Raw());  // !!! not in R3-Alpha

        Copy_Cell(OUT, v);  // before index modification
        REBLEN limit = Part_Len_May_Modify_Index(v, ARG(part));
        if (limit <= 1)
            return OUT;

        Length len;
        Size size;
        const Byte* utf8 = Cell_Utf8_Len_Size_At_Limit(&len, &size, v, limit);

        // Test for if the range is all ASCII can just be if (len == size)...
        // that means every codepoint is one byte.
        //
        if (len != size)
            fail ("Non-ASCII string sorting temporarily unavailable");

        REBLEN skip;
        if (not REF(skip))
            skip = 1;
        else {
            skip = Get_Num_From_Arg(ARG(skip));
            if (skip <= 0 or len % skip != 0 or skip > len)
                fail (PARAM(skip));
        }

        // Use fast quicksort library function:
        REBLEN span = 1;
        if (skip > 1) {
            len /= skip;
            span *= skip;
        }

        REBLEN thunk = 0;
        if (REF(case))
            thunk |= CC_FLAG_CASE;
        if (REF(reverse))
            thunk |= CC_FLAG_REVERSE;

        reb_qsort_r(
            m_cast(Byte*, utf8),  // ok due to cell mutability check
            len,
            span * sizeof(Byte),
            &thunk,
            Compare_Chr
        );
        return OUT; }

      case SYM_RANDOM: {
        INCLUDE_PARAMS_OF_RANDOM;

        UNUSED(PARAM(value));

        if (REF(seed)) { // string/binary contents are the seed
            assert(Any_String(v));

            Size utf8_size;
            Utf8(const*) utf8 = Cell_Utf8_Size_At(&utf8_size, v);
            Set_Random(crc32_z(0L, utf8, utf8_size));
            return NONE;
        }

        REBLEN index = VAL_INDEX(v);
        REBLEN tail = Cell_Series_Len_Head(v);

        if (REF(only)) {
            if (index >= tail)
                return nullptr;
            index += cast(REBLEN, Random_Int(REF(secure)))
                % (tail - index);

            return Init_Char_Unchecked(
                OUT,
                Get_Char_At(Cell_String(v), index)
            );
        }

        String* str = Cell_String_Ensure_Mutable(v);

        if (not Is_String_Definitely_ASCII(str))
            fail ("UTF-8 Everywhere: String shuffle temporarily unavailable");

        bool secure = REF(secure);

        REBLEN n;
        for (n = String_Len(str) - index; n > 1;) {
            REBLEN k = index + cast(REBLEN, Random_Int(secure)) % n;
            n--;
            Codepoint swap = Get_Char_At(str, k);
            Set_Char_At(str, k, Get_Char_At(str, n + index));
            Set_Char_At(str, n + index, swap);
        }
        return COPY(v); }

      default:
        // Let the port system try the action, e.g. OPEN %foo.txt
        //
        if ((Is_File(v) or Is_Url(v))) {
            const REBVAL *made = rebValue("make port! @", D_ARG(1));
            assert(Is_Port(made));
            Copy_Cell(D_ARG(1), made);
            rebRelease(made);
            return BOUNCE_CONTINUE;
        }
    }

    fail (UNHANDLED);
}


//
//  Startup_String: C
//
void Startup_String(void)
{
    Char_Escapes = Try_Alloc_N_Zerofill(Byte, MAX_ESC_CHAR + 1);

    Byte* cp = Char_Escapes;
    Byte c;
    for (c = '@'; c <= '_'; c++)
        *cp++ = c;

    Char_Escapes[cast(Byte, '\t')] = '-'; // tab
    Char_Escapes[cast(Byte, '\n')] = '/'; // line feed
    Char_Escapes[cast(Byte, '"')] = '"';
    Char_Escapes[cast(Byte, '^')] = '^';

    URL_Escapes = Try_Alloc_N_Zerofill(Byte, MAX_URL_CHAR + 1);

    for (c = 0; c <= ' '; c++)
        URL_Escapes[c] = ESC_URL | ESC_FILE;

    const Byte* dc = cb_cast(";%\"()[]{}<>");

    for (c = strsize(dc); c > 0; c--)
        URL_Escapes[*dc++] = ESC_URL | ESC_FILE;
}


//
//  Shutdown_String: C
//
void Shutdown_String(void)
{
    Free_N(Byte, MAX_ESC_CHAR + 1, Char_Escapes);
    Free_N(Byte, MAX_URL_CHAR + 1, URL_Escapes);
}
