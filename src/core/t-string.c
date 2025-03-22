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

Byte* g_char_escapes;
#define MAX_ESC_CHAR (0x60-1) // size of escape table
#define IS_CHR_ESC(c) ((c) <= MAX_ESC_CHAR and g_char_escapes[c])

Byte* g_url_escapes;
#define MAX_URL_CHAR (0x80-1)
#define IS_URL_ESC(c)  ((c) <= MAX_URL_CHAR and (g_url_escapes[c] & ESC_URL))
#define IS_FILE_ESC(c) ((c) <= MAX_URL_CHAR and (g_url_escapes[c] & ESC_FILE))

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

    if (
        Is_String_All_Ascii(s)
        and not SPORADICALLY(20)  // test non-ASCII codepath for ASCII
    ){
        possibly(Link_Bookmarks(s));  // mutations maintain for long strings
        return cast(Utf8(*), cast(Byte*, String_Head(s)) + at);
    }

    Utf8(*) cp;  // can be used to calculate offset (relative to String_Head())
    REBLEN index;

    Option(BookmarkList*) book = nullptr;  // updated at end if not nulled out
    if (not Is_String_Symbol(s))
        book = Link_Bookmarks(s);

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
        if (len < Size_Of(Cell)) {
            if (not Is_String_Symbol(s))
                assert(
                    Get_Stub_Flag(s, DYNAMIC)  // e.g. mold buffer
                    or not book  // mutations must ensure this
                );
            goto scan_from_head;  // good locality, avoid bookmark logic
        }
        if (not book and not Is_String_Symbol(s)) {
            book = Alloc_BookmarkList();
            Tweak_Link_Bookmarks(m_cast(String*, s), book);
            goto scan_from_head;  // will fill in bookmark
        }
    }
    else {
        if (len < Size_Of(Cell)) {
            if (not Is_String_Symbol(s))
                assert(
                    not book  // mutations must ensure this usually but...
                    or Get_Stub_Flag(s, DYNAMIC)  // !!! mold buffer?
                );
            goto scan_from_tail;  // good locality, avoid bookmark logic
        }
        if (not book and not Is_String_Symbol(s)) {
            book = Alloc_BookmarkList();
            Tweak_Link_Bookmarks(m_cast(String*, s), book);
            goto scan_from_tail;  // will fill in bookmark
        }
    }

    // Theoretically, a large UTF-8 string could have multiple "bookmarks".
    // That would complicate this logic by having to decide which one was
    // closest to be using.  For simplicity we just use one right now to
    // track the last access--which speeds up the most common case of an
    // iteration.  Improve as time permits!
    //
    assert(not book or Flex_Used(unwrap book) == 1);  // only one

  blockscope {
    REBLEN booked = book ? BOOKMARK_INDEX(unwrap book) : 0;

    // `at` is always positive.  `booked - at` may be negative, but if it
    // is positive and bigger than `at`, faster to seek from head.
    //
    if (at < booked - at) {
        if (at < Size_Of(Cell))
            book = nullptr;  // don't update bookmark for near head search
        goto scan_from_head;
    }

    // `len - at` is always positive.  `at - booked` may be negative, but if
    // it is positive and bigger than `len - at`, faster to seek from tail.
    //
    if ((len - at) < at - booked) {
        if (len - at < Size_Of(Cell))
            book = nullptr;  // don't update bookmark for near tail search
        goto scan_from_tail;
    }

    index = booked;
    if (book)
        cp = cast(Utf8(*), Flex_Data(s) + BOOKMARK_OFFSET(unwrap book));
    else
        cp = cast(Utf8(*), Flex_Data(s));
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
    BOOKMARK_INDEX(unwrap book) = index;
    BOOKMARK_OFFSET(unwrap book) = cp - String_Head(s);

  #if DEBUG_VERIFY_STR_AT
    Utf8(*) check_cp = String_Head(s);
    REBLEN check_index = 0;
    for (; check_index != at; ++check_index)
        check_cp = Skip_Codepoint(check_cp);
    assert(check_cp == cp);
  #endif

    return cp;
}


/***********************************************************************
**
**  Local Utility Functions
**
***********************************************************************/

// 1. !!! This is an inefficient method for reversing strings with variable
//    size codepoints.  Better way could work in place:
//
//      https://stackoverflow.com/q/199260/
//
static void Reverse_String(String* str, REBLEN index, Length len)
{
    if (len <= 1)
        return;  // zero or one characters means reverse is a noop

    if (
        Is_String_All_Ascii(str)
        and not SPORADICALLY(3)  // test non-ASCII code path on ASCII
    ){
        Byte* bp = String_At(str, index);

        REBLEN n = 0;
        REBLEN m = len - 1;
        for (; n < len / 2; n++, m--) {
            Byte b = bp[n];
            bp[n] = bp[m];
            bp[m] = b;
        }
    }
    else {  // !!! inefficient reversal for variable-size codepoints [1]
        DECLARE_MOLDER (mo);
        Push_Mold(mo);

        Length len_head = String_Len(str);  // should be same after we're done

        Utf8(const*) utf8 = String_Tail(str);  // last exists due to len != 0
        Count n;
        for (n = 0; n < len; ++n) {
            Codepoint c;
            utf8 = Utf8_Back(&c, utf8);
            Append_Codepoint(mo->string, c);
        }

        DECLARE_VALUE (temp);
        Init_Text(temp, Pop_Molded_String(mo));

        DECLARE_VALUE (string);  // !!! Temp value, string type is irrelevant
        Init_Any_String_At(string, TYPE_TEXT, str, index);
        Modify_String_Or_Binary(  // CHANGE:PART to overwrite reversed portion
            string,
            SYM_CHANGE,
            temp,
            AM_PART,  // heed len for deletion
            len,
            1 // dup count
        );

        assert(Cell_Series_Len_Head(string) == len_head);  // shouldn't change
        UNUSED(len_head);
    }
}


// 1. IMPLEMENT_GENERIC(MAKE, Is_Issue) calls GENERIC_CFUNC(MAKE, Any_String)
//    in its implementation.
//
// 2. !!! We can't really know how many bytes to allocate for a certain
//    number of codepoints.  UTF-8 may take up to UNI_ENCODED_MAX bytes
//    (typically 4) per CHAR!.  For now we just assume the integer is the
//    expected *byte* capacity, not length, as we can do that.
//
IMPLEMENT_GENERIC(MAKE, Any_String)
{
    INCLUDE_PARAMS_OF_MAKE;

    Heart heart = Cell_Datatype_Heart(ARG(TYPE));
    assert(Any_String_Type(heart) or Any_Utf8_Type(heart));  // issue calls [1]

    Element* def = Element_ARG(DEF);

    if (Is_Integer(def))  // new string with given integer capacity [2]
        return Init_Any_String(OUT, heart, Make_String(Int32s(def, 0)));

    return RAISE(Error_Bad_Make(heart, def));
}


//
//  /to-text: native [
//
//  "Variant of TO TEXT! with option to tolerate invisible codepoints"
//
//      return: [~null~ text!]
//      value [<maybe> element?]
//      :relax "Allow invisible codepoints like CR when converting BLOB!"
//  ]
//
DECLARE_NATIVE(TO_TEXT)
{
    INCLUDE_PARAMS_OF_TO_TEXT;

    if (Is_Blob(ARG(VALUE)) and Bool_ARG(RELAX)) {
        Size size;
        const Byte* at = Cell_Blob_Size_At(&size, ARG(VALUE));
        return Init_Any_String(
            OUT,
            TYPE_TEXT,
            Append_UTF8_May_Fail(
                nullptr,
                cs_cast(at),
                size,
                STRMODE_ALL_CODEPOINTS
            )
        );
    }

    return rebValue("to text! @", ARG(VALUE));
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
        *(--bp) = g_hex_digits[n & 0xf];
        n >>= 4;
    }

    while (bp < &buffer[10])
        *out++ = *bp++;

    return out;
}


//
//  Mold_Codepoint: C
//
// !!! These heuristics were used in R3-Alpha to decide when to output
// characters in strings as escape for molding.  It's not clear where to
// draw the line with it...should most printable characters just be emitted
// normally in the UTF-8 string with a few exceptions (like newline as ^/)?
//
// For now just preserve what was there, but do it as UTF8 bytes.
//
void Mold_Codepoint(Molder* mo, Codepoint c, bool non_ascii_parened)
{
    String* buf = mo->string;

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
        if (non_ascii_parened or c == 0x1E or c == 0xFEFF) {
            Append_Ascii(buf, "^(");

            Length len_old = String_Len(buf);
            Size size_old = String_Size(buf);
            Expand_Flex_Tail(buf, 5);  // worst case: ^(1234), ^( is done
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
    Append_Codepoint(buf, g_char_escapes[c]);
}


//
//  Mold_Text_Flex_At: C
//
void Mold_Text_Flex_At(Molder* mo, const String* s, REBLEN index) {
    String* buf = mo->string;

    if (index >= String_Len(s)) {
        Append_Ascii(buf, "\"\"");
        return;
    }

    Length len = String_Len(s) - index;

    bool non_ascii_parened = true;  // !!! used to be set to MOLD's :ALL flag

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

    if (not non_ascii_parened)
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
            Mold_Codepoint(mo, c, non_ascii_parened);
        }

        Append_Codepoint(buf, '"');
        return;
    }

    // It is a braced string, emit it as {string}:
    if (malign == 0)
        brace_in = brace_out = 0;

    Append_Codepoint(buf, '-');
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
            Mold_Codepoint(mo, c, non_ascii_parened);
        }
    }

    Append_Codepoint(buf, '}');
    Append_Codepoint(buf, '-');

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
IMPLEMENT_GENERIC(MOLDIFY, Is_Url)
{
    INCLUDE_PARAMS_OF_MOLDIFY;

    Element* v = Element_ARG(ELEMENT);
    Molder* mo = Cell_Handle_Pointer(Molder, ARG(MOLDER));
    bool form = Bool_ARG(FORM);

    UNUSED(form);
    Append_Any_Utf8(mo->string, v);

    return NOTHING;
}


IMPLEMENT_GENERIC(MOLDIFY, Is_Email)
{
    INCLUDE_PARAMS_OF_MOLDIFY;

    Element* v = Element_ARG(ELEMENT);
    Molder* mo = Cell_Handle_Pointer(Molder, ARG(MOLDER));
    bool form = Bool_ARG(FORM);

    UNUSED(form);
    Append_Any_Utf8(mo->string, v);

    return NOTHING;
}


static void Mold_File(Molder* mo, const Cell* v)
{

    Append_Codepoint(mo->string, '%');

    REBLEN len;
    Utf8(const*) cp = Cell_Utf8_Len_Size_At(&len, nullptr, v);

    REBLEN n;
    for (n = 0; n < len; ++n) {
        Codepoint c;
        cp = Utf8_Next(&c, cp);

        if (IS_FILE_ESC(c))
            Form_Hex_Esc(mo, c); // c => %xx
        else
            Append_Codepoint(mo->string, c);
    }
}


static void Mold_Tag(Molder* mo, const Cell* v)
{
    Append_Codepoint(mo->string, '<');
    Append_Any_Utf8(mo->string, v);
    Append_Codepoint(mo->string, '>');
}


// 1. The R3-Alpha forming logic was that every string type besides TAG!
//    would form with no delimiters, e.g. `form #foo` is just foo.  Ren-C
//    removes the exception for tags, and more of the system treats tag
//    as a normal string.  You have to quote it in FIND to get it to
//    use the molded semantics, for instance:
//
//        >> find "ab<c>d" <c>
//        == "c>d"
//
//        >> find "ab<c>d" quote <c>
//        == "<c>d"
//
IMPLEMENT_GENERIC(MOLDIFY, Any_String)
{
    INCLUDE_PARAMS_OF_MOLDIFY;

    Element* v = Element_ARG(ELEMENT);
    Molder* mo = Cell_Handle_Pointer(Molder, ARG(MOLDER));
    bool form = Bool_ARG(FORM);

    String* buf = mo->string;

    Heart heart = Heart_Of(v);
    assert(Any_Utf8_Type(heart));

    if (form) {  // TAG! is not an exception--forms without delimiters [1]
        Append_Any_Utf8(buf, v);
        return NOTHING;
    }

    switch (heart) {
      case TYPE_TEXT:
        Mold_Text_Flex_At(mo, Cell_String(v), VAL_INDEX(v));
        break;

      case TYPE_FILE:
        if (Cell_String_Len_At(v) == 0) {
            Append_Ascii(buf, "%\"\"");
            break;
        }
        Mold_File(mo, v);
        break;

      case TYPE_TAG:
        Mold_Tag(mo, v);
        break;

      default:
        panic (v);
    }

    return NOTHING;
}


//
//  Try_Get_Series_Index_From_Picker: C
//
// Will fail if the picker is outright invalid, but return false if it should
// be NULL on the last step of a PICK.
//
bool Try_Get_Series_Index_From_Picker(
    REBINT *out,
    const Element* v,
    const Element* picker
){
    if (not (Is_Integer(picker) or Is_Decimal(picker)))  // !!! why DECIMAL! ?
        fail (Error_Bad_Pick_Raw(picker));

    REBINT n = Int32(picker);
    if (n == 0)
        return false;  // Rebol2 and Red pick of 0 is none

    if (n < 0)
        ++n;

    n += VAL_INDEX_STRINGLIKE_OK(v) - 1;

    if (n < 0 or n >= Cell_Series_Len_Head(v))
        return false;  // out of range, null unless POKE or more PICK-ing

    *out = n;
    return true;
}


// 1. When things like ISSUE! or URL! have a node, their considerations are
//    not different from strings.  Their cell format has room for an index,
//    and that index is valid.  The special case of TO conversions is written
//    here so that non-node-having entities work.
//
IMPLEMENT_GENERIC(OLDGENERIC, Any_String)
{
    const Symbol* verb = Level_Verb(LEVEL);
    Option(SymId) id = Symbol_Id(verb);

    Element* v = cast(Element*, ARG_N(1));
    assert(Any_String(v));

    switch (id) {
      case SYM_REMOVE: {
        INCLUDE_PARAMS_OF_REMOVE;

        UNUSED(PARAM(SERIES)); // already accounted for

        String* s = Cell_String_Ensure_Mutable(v);

        REBINT limit;
        if (Bool_ARG(PART))
            limit = Part_Len_May_Modify_Index(v, ARG(PART));
        else
            limit = 1;

        REBLEN index = VAL_INDEX(v);  // Part calculation may have changed!
        REBLEN tail = Cell_Series_Len_Head(v);

        if (index >= tail or limit == 0)
            return COPY(v);

        Length len;
        Size size = Cell_String_Size_Limit_At(&len, v, &limit);

        Size offset = VAL_BYTEOFFSET_FOR_INDEX(v, index);
        Size size_old = String_Size(s);

        Remove_Flex_Units(s, offset, size);  // !!! at one time, kept term
        Free_Bookmarks_Maybe_Null(s);
        Term_String_Len_Size(s, tail - len, size_old - size);

        return COPY(v); }

    //-- Modification:
      case SYM_APPEND:
      case SYM_INSERT:
      case SYM_CHANGE: {
        INCLUDE_PARAMS_OF_INSERT;
        UNUSED(PARAM(SERIES));

        Value* arg = ARG(VALUE);
        assert(not Is_Nulled(arg));  // not an ~null~ parameter

        REBLEN len; // length of target
        if (Symbol_Id(verb) == SYM_CHANGE)
            len = Part_Len_May_Modify_Index(v, ARG(PART));
        else
            len = Part_Limit_Append_Insert(ARG(PART));

        // Note that while inserting or appending NULL is a no-op, CHANGE with
        // a :PART can actually erase data.
        //
        if (Is_Void(arg) and len == 0) {
            if (id == SYM_APPEND) // append always returns head
                VAL_INDEX_RAW(v) = 0;
            return COPY(v);  // don't fail on read only if would be a no-op
        }

        Flags flags = 0;
        if (Bool_ARG(PART))
            flags |= AM_PART;
        if (Bool_ARG(LINE))
            flags |= AM_LINE;

        // !!! This mimics historical type tolerance, e.g. not everything that
        // gets appended has to be a string:
        //
        //     rebol2>> append "abc" 'd
        //     == "abcd"
        //
        // However it will not try to FORM blocks or other arrays; it only
        // accepts antiform blocks to imply "append each item individually".
        //
        if (Is_Void(arg)) {
            // not necessarily a no-op (e.g. CHANGE can erase)
        }
        else if (Is_Splice(arg)) {
            QUOTE_BYTE(arg) = NOQUOTE_1;
        }
        else if (Any_List(arg))
            return FAIL(ARG(VALUE));  // no `append "abc" [d e]` w/o SPREAD
        else
            assert(not Is_Antiform(arg));

        VAL_INDEX_RAW(v) = Modify_String_Or_Binary(  // does read-only check
            v,
            unwrap id,
            arg,
            flags,
            len,
            Bool_ARG(DUP) ? Int32(ARG(DUP)) : 1
        );
        return COPY(v); }

    //-- Search:
      case SYM_SELECT:
      case SYM_FIND: {
        INCLUDE_PARAMS_OF_FIND;
        if (Is_Antiform(ARG(PATTERN)))
            return FAIL(PARAM(PATTERN));

        UNUSED(PARAM(SERIES));

        Flags flags = (
            (Bool_ARG(MATCH) ? AM_FIND_MATCH : 0)
            | (Bool_ARG(CASE) ? AM_FIND_CASE : 0)
        );

        REBLEN tail = Part_Tail_May_Modify_Index(v, ARG(PART));

        REBINT skip;
        if (Bool_ARG(SKIP)) {
            skip = VAL_INT32(ARG(SKIP));
            if (skip == 0)
                return FAIL(PARAM(SKIP));
        }
        else
            skip = 1;

        REBLEN len;
        REBINT find = Find_Value_In_Binstr(
            &len, v, tail, Element_ARG(PATTERN), flags, skip
        );

        if (find == NOT_FOUND)
            return nullptr;

        REBLEN ret = find;
        assert(ret <= tail);
        UNUSED(find);

        if (id == SYM_FIND) {
            Source* pack = Make_Source_Managed(2);
            Set_Flex_Len(pack, 2);

            Copy_Meta_Cell(Array_At(pack, 0), v);
            VAL_INDEX_RAW(Array_At(pack, 0)) = ret;

            Copy_Meta_Cell(Array_At(pack, 1), v);
            VAL_INDEX_RAW(Array_At(pack, 1)) = ret + len;

            return Init_Pack(OUT, pack);
        }
        else
            assert(id == SYM_SELECT);

        ++ret;
        if (ret == tail)
            return nullptr;

        return Init_Char_Unchecked(
            OUT,
            Codepoint_At(String_At(Cell_String(v), ret))
        ); }

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
        if (index == 0 and Get_Stub_Flag(s, DYNAMIC))
            Unbias_Flex(s, false);

        Free_Bookmarks_Maybe_Null(s);  // review!
        Size offset = VAL_BYTEOFFSET_FOR_INDEX(v, index);
        Free_Bookmarks_Maybe_Null(s);

        Term_String_Len_Size(s, index, offset);
        return COPY(v); }

    //-- Special actions:

      case SYM_SWAP: {
        Value* arg = ARG_N(2);

        if (Type_Of(v) != Type_Of(arg))
            return FAIL(Error_Not_Same_Type_Raw());

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

      default:
        // Let the port system try the action, e.g. OPEN %foo.txt
        //
        if ((Is_File(v) or Is_Url(v))) {
            const Value* made = rebValue("make port! @", ARG_N(1));
            assert(Is_Port(made));
            Copy_Cell(ARG_N(1), made);
            rebRelease(made);
            return BOUNCE_CONTINUE;
        }
    }

    return UNHANDLED;
}


// TO conversions of strings make copies (if the destination is mutable),
// and hence need only use read routines like Cell_Utf8_XXX() to access
// the bytes.  The ANY-UTF8? handler needs to deal with cells that might
// use storage in their cell for the data, or an allocated node (as all
// strings do).  Hence its concerns are a superset of those for strings.
//
// (We could just omit a TO handler here and it would fall through to
// the ANY-UTF8? generic, but this gives an opportunity to inject debug
// code and also to explain why there's not specific code for strings.)
//
IMPLEMENT_GENERIC(TO, Any_String)
{
    INCLUDE_PARAMS_OF_TO;

    USED(ARG(ELEMENT));
    USED(ARG(TYPE));

    return GENERIC_CFUNC(TO, Any_Utf8)(LEVEL);
}


//
//  Trap_Any_String_As: C
//
// 1. The reason that strings have a different AS aliasing is that it keeps
//    the AS ANY-UTF8? generic from having to worry about converting types
//    with indices.
//
Option(Error*) Trap_Any_String_As(
    Sink(Element) out,
    const Element* any_string,
    Heart as
){
    if (Any_String_Type(as)) {  // special handling not in Utf8 generic [1]
        Copy_Cell(out, any_string);
        HEART_BYTE(out) = as;
        Inherit_Const(out, any_string);
        return nullptr;
    }

    return Trap_Alias_Any_Utf8_As(out, any_string, as);
}


IMPLEMENT_GENERIC(AS, Any_String)
{
    INCLUDE_PARAMS_OF_AS;

    Option(Error*) e = Trap_Any_String_As(
        OUT,
        Element_ARG(ELEMENT),
        Cell_Datatype_Heart(ARG(TYPE))
    );
    if (e)
        return FAIL(unwrap e);

    return OUT;
}


IMPLEMENT_GENERIC(COPY, Any_String)
{
    INCLUDE_PARAMS_OF_COPY;

    Element* any_string = Element_ARG(VALUE);

    UNUSED(Bool_ARG(DEEP));  // :DEEP is historically ignored on ANY-STRING?

    REBINT len = Part_Len_May_Modify_Index(any_string, ARG(PART));

    return Init_Any_String(
        OUT,
        Heart_Of_Fundamental(any_string),
        Copy_String_At_Limit(any_string, &len)
    );
}


IMPLEMENT_GENERIC(PICK, Any_String)
{
    INCLUDE_PARAMS_OF_PICK;

    const Element* any_string = Element_ARG(LOCATION);
    const Element* picker = Element_ARG(PICKER);

    REBINT n;
    if (not Try_Get_Series_Index_From_Picker(&n, any_string, picker))
        return RAISE(Error_Bad_Pick_Raw(picker));

    Codepoint c = Get_Char_At(Cell_String(any_string), n);

    return Init_Char_Unchecked(OUT, c);
}


IMPLEMENT_GENERIC(POKE, Any_String)
{
    INCLUDE_PARAMS_OF_POKE;

    Element* any_string = Element_ARG(LOCATION);

    const Element* picker = Element_ARG(PICKER);
    REBINT n;
    if (not Try_Get_Series_Index_From_Picker(&n, any_string, picker))
        return FAIL(Error_Out_Of_Range(picker));

    Value* poke = ARG(VALUE);

    Codepoint c;
    if (IS_CHAR(poke)) {
        c = Cell_Codepoint(poke);
    }
    else if (Is_Integer(poke)) {
        c = Int32(poke);
    }
    else  // CHANGE is a better route for splicing/removal/etc.
        return FAIL(PARAM(VALUE));

    if (c == 0)
        return FAIL(Error_Illegal_Zero_Byte_Raw());

    String* s = Cell_String_Ensure_Mutable(any_string);
    Set_Char_At(s, n, c);

    return nullptr;  // String* in Cell unchanged, caller need not update
}


IMPLEMENT_GENERIC(TAKE, Any_String)
{
    INCLUDE_PARAMS_OF_TAKE;

    Element* v = Element_ARG(SERIES);
    Ensure_Mutable(v);

    if (Bool_ARG(DEEP))
        return FAIL(Error_Bad_Refines_Raw());

    REBLEN len;
    if (Bool_ARG(PART)) {
        len = Part_Len_May_Modify_Index(v, ARG(PART));
        if (len == 0) {
            Heart heart = Heart_Of_Fundamental(v);
            return Init_Any_String(OUT, heart, Make_String(0));
        }
    } else
        len = 1;

    // Note that :PART can change index

    REBLEN tail = Cell_Series_Len_Head(v);

    if (Bool_ARG(LAST)) {
        if (len > tail) {
            VAL_INDEX_RAW(v) = 0;
            len = tail;
        }
        else
            VAL_INDEX_RAW(v) = tail - len;
    }

    if (VAL_INDEX(v) >= tail) {
        if (not Bool_ARG(PART))
            return RAISE(Error_Nothing_To_Take_Raw());
        Heart heart = Heart_Of_Fundamental(v);
        return Init_Any_String(OUT, heart, Make_String(0));
    }

    // if no :PART, just return value, else return string
    //
    if (Bool_ARG(PART)) {
        Heart heart = Heart_Of_Fundamental(v);
        Init_Any_String(OUT, heart, Copy_String_At_Limit(v, &len));
    }
    else
        Init_Char_Unchecked(OUT, Codepoint_At(Cell_String_At(v)));

    Remove_Any_Series_Len(v, VAL_INDEX(v), len);
    return OUT;
}


IMPLEMENT_GENERIC(REVERSE, Any_String)
{
    INCLUDE_PARAMS_OF_REVERSE;

    Element* any_string = Element_ARG(SERIES);

    String* s = Cell_String_Ensure_Mutable(any_string);

    Copy_Cell(OUT, any_string);  // save before index adjustment
    REBINT len = Part_Len_May_Modify_Index(any_string, ARG(PART));
    Reverse_String(s, VAL_INDEX(any_string), len);
    return OUT;
}


IMPLEMENT_GENERIC(RANDOM_PICK, Any_String)
{
    INCLUDE_PARAMS_OF_RANDOM_PICK;

    Element* v = Element_ARG(COLLECTION);

    REBLEN index = VAL_INDEX(v);
    REBLEN tail = Cell_Series_Len_Head(v);

    if (index >= tail)
        return RAISE(Error_Bad_Pick_Raw(Init_Integer(SPARE, 0)));

    index += Random_Int(Bool_ARG(SECURE)) % (tail - index);

    return Init_Char_Unchecked(
        OUT,
        Get_Char_At(Cell_String(v), index)
    );
}


// 1. It hasn't been a priority to write a fast shuffle algorithm for non
//    ASCII strings.  (Or even ASCII ones really, but the code existed in
//    R3-Alpha for that and so it was kept).  It's of little concern, so
//    if there's any non-ASCII codepoints we just use MAP-EACH to make
//    new shuffled data to replace in the string up to the tail.
//
IMPLEMENT_GENERIC(SHUFFLE, Any_String)
{
    INCLUDE_PARAMS_OF_SHUFFLE;

    Element* any_string = Element_ARG(SERIES);

    REBLEN index = VAL_INDEX(any_string);

    String* str = Cell_String_Ensure_Mutable(any_string);

    if (not Is_String_All_Ascii(str))  // slow is better than not at all [1]
        return rebDelegate(
            "let shuffled: unspaced shuffle map-each 'c", any_string, "[c]"
            "take:part", any_string, "tail of", any_string,  // drop tail
            "append", any_string, "shuffled",  // add shuffled bit
            any_string  // return string at original position
        );

    bool secure = Bool_ARG(SECURE);

    REBLEN n;
    for (n = String_Len(str) - index; n > 1;) {
        REBLEN k = index + (Random_Int(secure) % n);
        n--;
        Codepoint swap = Get_Char_At(str, k);
        Set_Char_At(str, k, Get_Char_At(str, n + index));
        Set_Char_At(str, n + index, swap);
    }
    return COPY(any_string);
}


IMPLEMENT_GENERIC(CODEPOINT_OF, Any_String)
{
    INCLUDE_PARAMS_OF_CODEPOINT_OF;

    Element* str = Element_ARG(ELEMENT);
    const Byte* bp = Cell_String_At(str);  // downgrade validated Utf8(*)

    Codepoint c;
    if (
        *bp != '\0'  // can't be at tail
        and (
            bp = Back_Scan_Utf8_Char_Unchecked(&c, bp),
            ++bp  // Back_Scan() needs increment
        )
        and *bp == '\0'  // after one scan, must be at tail
    ){
        return Init_Integer(OUT, c);
    }
    return RAISE(Error_Not_One_Codepoint_Raw());
}


enum COMPARE_CHR_FLAGS {
    CC_FLAG_CASE = 1 << 0, // Case sensitive sort
    CC_FLAG_REVERSE = 1 << 1 // Reverse sort order
};


// This function is called by qsort_r, on behalf of the string sort
// function.  The `state` is an argument passed through from the caller
// and given to us by the sort routine, which tells us about the string
// and the kind of sort that was requested.
//
// !!! As of UTF-8 everywhere, this will only work on all-ASCII strings.
//
static int Qsort_Char_Callback(void *state, const void *v1, const void *v2)
{
    Flags* flags = cast(Flags*, state);

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


IMPLEMENT_GENERIC(SORT, Any_String)
{
    INCLUDE_PARAMS_OF_SORT;

    Element* v = Element_ARG(SERIES);
    String* str = Cell_String_Ensure_Mutable(v);  // just ensure mutability
    UNUSED(str);  // we use the Cell_Utf8_At() accessor, which is const

    if (Bool_ARG(ALL))
        return FAIL(Error_Bad_Refines_Raw());

    if (Bool_ARG(COMPARE))
        return FAIL(Error_Bad_Refines_Raw());  // !!! not in R3-Alpha

    Copy_Cell(OUT, v);  // before index modification
    REBLEN limit = Part_Len_May_Modify_Index(v, ARG(PART));
    if (limit <= 1)
        return OUT;

    Length len;
    Size size;
    const Byte* utf8 = Cell_Utf8_Len_Size_At_Limit(&len, &size, v, &limit);

    // Test for if the range is all ASCII can just be if (len == size)...
    // that means every codepoint is one byte.
    //
    if (len != size)
        return FAIL("Non-ASCII string sorting temporarily unavailable");

    REBLEN skip;
    if (not Bool_ARG(SKIP))
        skip = 1;
    else {
        skip = Get_Num_From_Arg(ARG(SKIP));
        if (skip <= 0 or len % skip != 0 or skip > len)
            return FAIL(PARAM(SKIP));
    }

    // Use fast quicksort library function:
    REBLEN span = 1;
    if (skip > 1) {
        len /= skip;
        span *= skip;
    }

    Flags flags = 0;
    if (Bool_ARG(CASE))
        flags |= CC_FLAG_CASE;
    if (Bool_ARG(REVERSE))
        flags |= CC_FLAG_REVERSE;

    bsd_qsort_r(
        m_cast(Byte*, utf8),  // ok due to cell mutability check
        len,
        span * sizeof(Byte),
        &flags,
        &Qsort_Char_Callback
    );
    return OUT;
}


//
//  /encode-UTF-8: native [
//      "Encode a string to the UTF-8 standard (see also AS TEXT!)"
//
//      return: [blob!]
//      arg [any-utf8?]
//      options "TBD: prohibit CR and TAB by default!"
//          [block!]
//  ]
//
DECLARE_NATIVE(ENCODE_UTF_8) {
    INCLUDE_PARAMS_OF_ENCODE_UTF_8;

    Value* arg = ARG(ARG);

    if (Cell_Series_Len_At(ARG(OPTIONS)))
        return FAIL("UTF-8 Encoder Options not Designed Yet");

    Size utf8_size;
    Utf8(const*) utf8 = Cell_Utf8_Size_At(&utf8_size, arg);

    Binary* b = Make_Binary(utf8_size);
    memcpy(Binary_Head(b), utf8, utf8_size);
    Term_Binary_Len(b, utf8_size);
    return Init_Blob(OUT, b);
}


//
//  /decode-UTF-8: native [
//      "Decode (and validate) bytes as text according to the UTF-8 standard"
//
//      return: [text!]  ; review ~NaN~, ~inf~ as antiforms
//      blob [blob!]
//      options "TBD: allow CR (off by default), other options?"
//          [block!]
//  ]
//
DECLARE_NATIVE(DECODE_UTF_8)
//
// 1. It's pretty easy to say (as tag! decode 'UTF8 some-binary).  Admittedly
//    that's longer than (to tag! some-binary) or (make tag! some-binary),
//    but it seems about as long as it needs to be... if you're saying that
//    a lot then make `utf8-to-tag` or `u8-to-t` or similar.
{
    INCLUDE_PARAMS_OF_DECODE_UTF_8;

    Element* blob = Element_ARG(BLOB);

    if (Cell_Series_Len_At(ARG(OPTIONS)))
        return FAIL("UTF-8 Decoder Options not Designed Yet");

    Heart heart = TYPE_TEXT;  // should options let you specify? [1]

    Size size;
    const Byte* at = Cell_Blob_Size_At(&size, blob);
    return Init_Any_String(
        OUT,
        heart,
        Append_UTF8_May_Fail(nullptr, cs_cast(at), size, STRMODE_NO_CR)
    );
}


//
//  Startup_String: C
//
void Startup_String(void)
{
    g_char_escapes = Try_Alloc_Memory_N_Zerofill(Byte, MAX_ESC_CHAR + 1);

    Byte* cp = g_char_escapes;
    Byte c;
    for (c = '@'; c <= '_'; c++)
        *cp++ = c;

    g_char_escapes[cast(Byte, '\t')] = '-'; // tab
    g_char_escapes[cast(Byte, '\n')] = '/'; // line feed
    g_char_escapes[cast(Byte, '"')] = '"';
    g_char_escapes[cast(Byte, '^')] = '^';

    g_url_escapes = Try_Alloc_Memory_N_Zerofill(Byte, MAX_URL_CHAR + 1);

    for (c = 0; c <= ' '; c++)
        g_url_escapes[c] = ESC_URL | ESC_FILE;

    const Byte* dc = cb_cast(";%\"()[]{}<>");

    for (c = strsize(dc); c > 0; c--)
        g_url_escapes[*dc++] = ESC_URL | ESC_FILE;
}


//
//  Shutdown_String: C
//
void Shutdown_String(void)
{
    Free_Memory_N(Byte, MAX_ESC_CHAR + 1, g_char_escapes);
    Free_Memory_N(Byte, MAX_URL_CHAR + 1, g_url_escapes);
}
