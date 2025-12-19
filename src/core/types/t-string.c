//
//  file: %t-string.c
//  summary: "string related datatypes"
//  section: datatypes
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

#include "sys-core.h"

#undef Byte  // sys-zlib.h defines it compatibly (unsigned char)
#include "sys-zlib.h"  // for crc32_z()

#include "sys-int-funcs.h"


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
//  Non_Const_Correct_Strand_At: C
//
// Note that we only ever create caches for strings that have had Strand_At()
// run on them.  So the more operations that avoid Strand_At(), the better!
// Using Strand_Head() and Strand_Tail() will give a Utf8(*) that can be used
// to iterate much faster, and most of the strings in the system might be able
// to get away with not having any bookmarks at all.
//
Utf8(*) Non_Const_Correct_Strand_At(const Strand* s, REBLEN at)
{
    assert(s != g_mold.buffer);  // Strand_At() makes bookmarks, don't want!

    assert(at <= Strand_Len(s));

    Utf8(const*) cp;  // can use to get offset (relative to Strand_Head())

    if (
        Is_Strand_All_Ascii(s)
        and not SPORADICALLY(20)  // test non-ASCII codepath for ASCII
    ){
        possibly(Link_Bookmarks(s));  // mutations maintain for long strings
        cp = u_cast(Utf8(const*), u_cast(Byte*, Strand_Head(s)) + at);
        return m_cast(Utf8(*), cp);
    }

    Index index;

    Option(BookmarkList*) book = nullptr;  // updated at end if not nulled out
    if (not Is_Strand_Symbol(s))
        book = Link_Bookmarks(s);

  #if DEBUG_SPORADICALLY_DROP_BOOKMARKS
    if (book and SPORADICALLY(100)) {
        Free_Bookmarks_Maybe_Null(s);
        book = nullptr;
    }
  #endif

    REBLEN len = Strand_Len(s);

  #if DEBUG_TRACE_BOOKMARKS
    BOOKMARK_TRACE("len %ld @ %ld ", len, at);
    BOOKMARK_TRACE("%s", bookmark ? "bookmarked" : "no bookmark");
  #endif

    if (at < len / 2) {
        if (len < Size_Of(Cell)) {
            if (not Is_Strand_Symbol(s))
                assert(
                    Get_Stub_Flag(s, DYNAMIC)  // e.g. mold buffer
                    or not book  // mutations must ensure this
                );
            goto scan_from_head;  // good locality, avoid bookmark logic
        }
        if (not book and not Is_Strand_Symbol(s)) {
            book = Alloc_BookmarkList();
            Tweak_Link_Bookmarks(m_cast(Strand*, s), book);
            goto scan_from_head;  // will fill in bookmark
        }
    }
    else {
        if (len < Size_Of(Cell)) {
            if (not Is_Strand_Symbol(s))
                assert(
                    not book  // mutations must ensure this usually but...
                    or Get_Stub_Flag(s, DYNAMIC)  // !!! mold buffer?
                );
            goto scan_from_tail;  // good locality, avoid bookmark logic
        }
        if (not book and not Is_Strand_Symbol(s)) {
            book = Alloc_BookmarkList();
            Tweak_Link_Bookmarks(m_cast(Strand*, s), book);
            goto scan_from_tail;  // will fill in bookmark
        }
    }

  decide_where_to_scan_from: { ///////////////////////////////////////////////

    // The bookmark may be helpful as where to scan from, but if the position
    // is closer to the head or the tail of the string, it may be faster to
    // scan from there.
    //
    // 1. Theoretically, a large UTF-8 string could have multiple "bookmarks".
    //    That would complicate this logic by having to decide which one was
    //    closest to be using.  For simplicity we just use one right now to
    //    track the last access--which speeds up the most common case of an
    //    iteration.  Improve as time permits!
    //
    // 2. `at` is always positive.  `booked - at` may be negative, but if it
    //    is positive and bigger than `at`, faster to seek from head.
    //
    // 3. `len - at` is always positive.  `at - booked` may be negative, but
    //    if positive and bigger than `len - at`, faster to seek from tail.

    assert(not book or Flex_Used(unwrap book) == 1);  // max of one [1]

    REBLEN booked = book ? BOOKMARK_INDEX(unwrap book) : 0;

    if (at < booked - at) {  // `at` is always positive [2]
        if (at < Size_Of(Cell))
            book = nullptr;  // don't update bookmark for near head search
        goto scan_from_head;
    }

    if ((len - at) < at - booked) {  // `len - at` is always positive [3]
        if (len - at < Size_Of(Cell))
            book = nullptr;  // don't update bookmark for near tail search
        goto scan_from_tail;
    }

    index = booked;
    if (book)
        cp = u_cast(Utf8(const*), Flex_Data(s) + BOOKMARK_OFFSET(unwrap book));
    else
        cp = u_cast(Utf8(const*), Flex_Data(s));

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

} scan_from_head: { //////////////////////////////////////////////////////////

  #if DEBUG_TRACE_BOOKMARKS
    BOOKMARK_TRACE("scan from head");
  #endif
    cp = Strand_Head(s);
    index = 0;

} scan_forward: { ////////////////////////////////////////////////////////////

    assert(index <= at);
    for (; index != at; ++index)
        cp = Skip_Codepoint(cp);

    if (not book)
        return m_cast(Utf8(*), cp);

    goto update_bookmark;

} scan_from_tail: { //////////////////////////////////////////////////////////

  #if DEBUG_TRACE_BOOKMARKS
    BOOKMARK_TRACE("scan from tail");
  #endif
    cp = Strand_Tail(s);
    index = len;

} scan_backward: { //////////////////////////////////////////////////////////

    assert(index >= at);
    for (; index != at; --index)
        cp = Step_Back_Codepoint(cp);

    if (not book) {
      #if DEBUG_TRACE_BOOKMARKS
        BOOKMARK_TRACE("not cached\n");
      #endif
        return m_cast(Utf8(*), cp);
    }

} update_bookmark: { /////////////////////////////////////////////////////////

  #if DEBUG_TRACE_BOOKMARKS
    BOOKMARK_TRACE("caching %ld\n", index);
  #endif
    BOOKMARK_INDEX(unwrap book) = index;
    BOOKMARK_OFFSET(unwrap book) = cp - Strand_Head(s);

  #if DEBUG_VERIFY_STR_AT
    Utf8(const*) check_cp = Strand_Head(s);
    REBLEN check_index = 0;
    for (; check_index != at; ++check_index)
        check_cp = Skip_Codepoint(check_cp);
    assert(check_cp == cp);
  #endif

    return m_cast(Utf8(*), cp);
}}


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
static void Reverse_Strand(Strand* str, Index index, Length len)
{
    if (len <= 1)
        return;  // zero or one characters means reverse is a noop

    if (
        Is_Strand_All_Ascii(str)
        and not SPORADICALLY(3)  // test non-ASCII code path on ASCII
    ){
        Byte* bp = Strand_At(str, index);

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

        Length len_head = Strand_Len(str);  // should be same after we're done

        Utf8(const*) utf8 = Strand_Tail(str);  // last exists due to len != 0
        Count n;
        for (n = 0; n < len; ++n) {
            Codepoint c;
            utf8 = Utf8_Back(&c, utf8);
            Append_Codepoint(mo->strand, c);
        }

        DECLARE_ELEMENT (temp);
        Init_Text(temp, Pop_Molded_Strand(mo));

        DECLARE_ELEMENT (string);  // !!! Temp value, string type is irrelevant
        Init_Any_String_At(string, TYPE_TEXT, str, index);
        require(
          Length tail = Modify_String_Or_Blob(
            string,
            ST_MODIFY_CHANGE,
            temp,
            (not AM_LINE),
            len,  // how much to delete (CHANGE:PART)
            1 // dup count
        ));

        assert(Series_Len_Head(string) == len_head);  // shouldn't change
        UNUSED(len_head);

        assert(tail == index + len);
        UNUSED(tail);
    }
}


// 1. IMPLEMENT_GENERIC(MAKE, Is_Rune) calls GENERIC_CFUNC(MAKE, Any_String)
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

    Heart heart = Datatype_Builtin_Heart(ARG(TYPE));
    assert(Any_String_Type(heart) or Any_Utf8_Type(heart));  // rune calls [1]

    Element* def = Element_ARG(DEF);

    if (Is_Integer(def)) { // new string with given integer capacity [2]
        require (
          Strand* strand = Make_Strand(Int32s(def, 0))
        );
        return Init_Any_String(OUT, heart, strand);
    }

    return fail (Error_Bad_Make(heart, def));
}


//
//  to-text: native [
//
//  "Variant of TO TEXT! with option to tolerate invisible codepoints"
//
//      return: [<null> text!]
//      value [<opt-out> element?]
//      :relax "Allow invisible codepoints like CR when converting BLOB!"
//  ]
//
DECLARE_NATIVE(TO_TEXT)
{
    INCLUDE_PARAMS_OF_TO_TEXT;

    if (Is_Blob(ARG(VALUE)) and ARG(RELAX)) {
        Size size;
        const Byte* at = Blob_Size_At(&size, ARG(VALUE));
        return Init_Any_String(
            OUT,
            TYPE_TEXT,
            Append_UTF8_May_Panic(
                nullptr,
                s_cast(at),
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
    Strand* buf = mo->strand;

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
            require (
              Append_Ascii(buf, "^(")
            );

            Length len_old = Strand_Len(buf);
            Size size_old = Strand_Size(buf);
            require (  // worst: ^(1234), ^( has already been added done
              Expand_Flex_Tail_And_Update_Used(buf, 5)
            );
            Term_Strand_Len_Size(buf, len_old, size_old);

            Byte* bp = Binary_Tail(buf);
            Byte* ep = Form_Uni_Hex(bp, c); // !!! Make a mold...
            Term_Strand_Len_Size(
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
void Mold_Text_Flex_At(Molder* mo, const Strand* s, Index index) {
    Strand* buf = mo->strand;

    if (index >= Strand_Len(s)) {
        require (
          Append_Ascii(buf, "\"\"")
        );
        return;
    }

    Length len = Strand_Len(s) - index;

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

    Utf8(const*) up = Strand_At(s, index);

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

    up = Strand_At(s, index);

    // If it is a short quoted string, emit it as "string"
    //
    if (len <= MAX_QUOTED_STR and quote == 0 and newline < 3) {
        Append_Codepoint(buf, '"');

        REBLEN n;
        for (n = index; n < Strand_Len(s); n++) {
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
    Append_Codepoint(buf, '[');

    REBLEN n;
    for (n = index; n < Strand_Len(s); n++) {
        Codepoint c;
        up = Utf8_Next(&c, up);

        switch (c) {
          case '[':
          case ']':
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

    Append_Codepoint(buf, ']');
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

    Element* v = Element_ARG(VALUE);
    Molder* mo = Cell_Handle_Pointer(Molder, ARG(MOLDER));
    bool form = did ARG(FORM);

    UNUSED(form);
    Append_Any_Utf8(mo->strand, v);

    return TRASH;
}


IMPLEMENT_GENERIC(MOLDIFY, Is_Email)
{
    INCLUDE_PARAMS_OF_MOLDIFY;

    Element* v = Element_ARG(VALUE);
    Molder* mo = Cell_Handle_Pointer(Molder, ARG(MOLDER));
    bool form = did ARG(FORM);

    UNUSED(form);
    Append_Any_Utf8(mo->strand, v);

    return TRASH;
}


static void Mold_File(Molder* mo, const Cell* v)
{
    Append_Codepoint(mo->strand, '%');

    REBLEN len;
    Utf8(const*) cp = Cell_Utf8_Len_Size_At(&len, nullptr, v);

    REBLEN n;
    for (n = 0; n < len; ++n) {
        Codepoint c;
        cp = Utf8_Next(&c, cp);

        // This was the old logic.  We could conceivably just remember if the
        // file had quotes to begin with.
        /*
        if (IS_FILE_ESC(c))
            Form_Hex_Esc(mo, c); // c => %xx
        else
            Append_Codepoint(mo->strand, c);
        */

        Append_Codepoint(mo->strand, c);
    }
}


static void Mold_Tag(Molder* mo, const Cell* v)
{
    Append_Codepoint(mo->strand, '<');
    Append_Any_Utf8(mo->strand, v);
    Append_Codepoint(mo->strand, '>');
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

    Element* v = Element_ARG(VALUE);
    Molder* mo = Cell_Handle_Pointer(Molder, ARG(MOLDER));
    bool form = did ARG(FORM);

    Strand* buf = mo->strand;

    Heart heart = Heart_Of_Builtin_Fundamental(v);
    assert(Any_Utf8_Type(heart));

    if (form) {  // TAG! is not an exception--forms without delimiters [1]
        Append_Any_Utf8(buf, v);
        return TRASH;
    }

    switch (heart) {
      case TYPE_TEXT:
        Mold_Text_Flex_At(mo, Cell_Strand(v), Series_Index(v));
        break;

      case TYPE_FILE:
        if (String_Len_At(v) == 0) {
            require (
              Append_Ascii(buf, "%\"\"")
            );
            break;
        }
        Mold_File(mo, v);
        break;

      case TYPE_TAG:
        Mold_Tag(mo, v);
        break;

      default:
        crash (v);
    }

    return TRASH;
}


//
//  Try_Get_Series_Index_From_Picker: C
//
// Will panic if the picker is outright invalid, but return false if it should
// be NULL on the last step of a PICK.
//
bool Try_Get_Series_Index_From_Picker(
    REBINT *out,
    const Element* v,
    const Stable* picker
){
    if (not (Is_Integer(picker) or Is_Decimal(picker)))  // !!! why DECIMAL! ?
        panic (Error_Bad_Pick_Raw(picker));

    REBINT n = Int32(picker);
    if (n == 0)
        return false;  // Rebol2 and Red pick of 0 is none

    if (n < 0)
        ++n;

    n += Series_Index_Stringlike_Ok(v) - 1;

    if (n < 0 or n >= Series_Len_Head(v))
        return false;  // out of range, null unless POKE or more PICK-ing

    *out = n;
    return true;
}


// 1. When things like RUNE! or URL! have a node, their considerations are
//    not different from strings.  Their cell format has room for an index,
//    and that index is valid.  The special case of TO conversions is written
//    here so that non-node-having entities work.
//
IMPLEMENT_GENERIC(OLDGENERIC, Any_String)
{
    const Symbol* verb = Level_Verb(LEVEL);
    Option(SymId) id = Symbol_Id(verb);

    Element* v = cast(Element*, ARG_N(1));
    assert(Any_String(v) or Any_Utf8(v));  // UTF-8 delegates, but immutable

    switch (opt id) {
      case SYM_REMOVE: {
        INCLUDE_PARAMS_OF_REMOVE;

        Strand* s = Cell_Strand_Ensure_Mutable(v);

        REBINT limit;
        if (ARG(PART))
            limit = Part_Len_May_Modify_Index(v, ARG(PART));
        else
            limit = 1;

        Index index = Series_Index(v);  // Part calculation may have changed!
        REBLEN tail = Series_Len_Head(v);

        if (index >= tail or limit == 0)
            return COPY(v);

        Length len;
        Size size = String_Size_Limit_At(&len, v, &limit);

        Size offset = String_Byte_Offset_For_Index(v, index);
        Size size_old = Strand_Size(s);

        Remove_Flex_Units_And_Update_Used(s, offset, size);
        Free_Bookmarks_Maybe_Null(s);
        Term_Strand_Len_Size(s, tail - len, size_old - size);

        return COPY(v); }

    //-- Search:
      case SYM_SELECT:
      case SYM_FIND: {
        INCLUDE_PARAMS_OF_FIND;

        if (Is_Antiform(ARG(PATTERN)))
            panic (PARAM(PATTERN));

        Flags flags = (
            (ARG(MATCH) ? AM_FIND_MATCH : 0)
            | (ARG(CASE) ? AM_FIND_CASE : 0)
        );

        REBLEN tail = Part_Tail_May_Modify_Index(v, ARG(PART));

        REBINT skip;
        if (ARG(SKIP)) {
            skip = VAL_INT32(unwrap ARG(SKIP));
            if (skip == 0)
                panic (PARAM(SKIP));
        }
        else
            skip = 1;

        REBLEN len;
        REBINT find = Find_Value_In_Binstr(
            &len, v, tail, Element_ARG(PATTERN), flags, skip
        );

        if (find == NOT_FOUND)
            return NULLED;

        REBLEN ret = find;
        assert(ret <= tail);
        UNUSED(find);

        if (id == SYM_FIND) {
            Source* pack = Make_Source_Managed(2);
            Set_Flex_Len(pack, 2);

            Copy_Lifted_Cell(Array_At(pack, 0), v);
            SERIES_INDEX_UNBOUNDED(Array_At(pack, 0)) = ret;

            Copy_Lifted_Cell(Array_At(pack, 1), v);
            SERIES_INDEX_UNBOUNDED(Array_At(pack, 1)) = ret + len;

            return Init_Pack(OUT, pack);
        }
        else
            assert(id == SYM_SELECT);

        ++ret;
        if (ret == tail)
            return NULLED;

        return Init_Char_Unchecked(
            OUT,
            Codepoint_At(Strand_At(Cell_Strand(v), ret))
        ); }

      case SYM_CLEAR: {
        Strand* s = Cell_Strand_Ensure_Mutable(v);

        Index index = Series_Index(v);
        REBLEN tail = Series_Len_Head(v);

        if (index >= tail)
            return COPY(v);  // clearing after available data has no effect

        // !!! R3-Alpha would take this opportunity to make it so that if the
        // series is now empty, it reclaims the "bias" (unused capacity at
        // the head of the series).  One of many behaviors worth reviewing.
        //
        if (index == 0 and Get_Stub_Flag(s, DYNAMIC))
            Unbias_Flex(s, false);

        Free_Bookmarks_Maybe_Null(s);  // review!
        Size offset = String_Byte_Offset_For_Index(v, index);
        Free_Bookmarks_Maybe_Null(s);

        Term_Strand_Len_Size(s, index, offset);
        return COPY(v); }

    //-- Special actions:

      case SYM_SWAP: {
        Stable* arg = ARG_N(2);

        if (Type_Of(v) != Type_Of(arg))
            panic (Error_Not_Same_Type_Raw());

        Strand* v_str = Cell_Strand_Ensure_Mutable(v);
        Strand* arg_str = Cell_Strand_Ensure_Mutable(arg);

        Index index = Series_Index(v);
        REBLEN tail = Series_Len_Head(v);

        if (index < tail and Series_Index(arg) < Series_Len_Head(arg)) {
            Codepoint v_c = Get_Strand_Char_At(v_str, Series_Index(v));
            Codepoint arg_c = Get_Strand_Char_At(arg_str, Series_Index(arg));

            require (
              Set_Char_At(v_str, Series_Index(v), arg_c)
            );
            require (
              Set_Char_At(arg_str, Series_Index(arg), v_c)
            );
        }
        return COPY(v); }

      default:
        // Let the port system try the action, e.g. OPEN %foo.txt
        //
        if ((Is_File(v) or Is_Url(v))) {
            Api(const Stable*) made = rebStable("make port! @", ARG_N(1));
            assert(Is_Port(made));
            Copy_Cell(ARG_N(1), made);
            rebRelease(made);
            return BOUNCE_CONTINUE;
        }
    }

    panic (UNHANDLED);
}


// See notes on CHANGE regarding questions of how much work is expected to be
// handled by the "front end" native vs. Modify_String_Or_Blob() as callable
// by C code that doesn't go through the native.
//
IMPLEMENT_GENERIC(CHANGE, Any_String)
{
    INCLUDE_PARAMS_OF_CHANGE;

    Length len = VAL_UINT32(unwrap ARG(PART));  // enforced > 0 by generic
    Count dups = VAL_UINT32(unwrap ARG(DUP));  // enforced > 0 by generic

    Flags flags = 0;
    if (ARG(LINE))
        flags |= AM_LINE;

    require (
      Length tail = Modify_String_Or_Blob(
        Element_ARG(SERIES),  // does read-only check
        u_cast(ModifyState, STATE),
        unwrap ARG(VALUE),
        flags,
        len,
        dups
    ));

    Element* out = Copy_Cell(OUT, Element_ARG(SERIES));
    SERIES_INDEX_UNBOUNDED(out) = tail;

    return OUT;
}


// TO conversions of strings make copies (if the destination is mutable),
// and hence need only use read routines like Cell_Utf8_XXX() to access
// the bytes.  The ANY-UTF8? handler needs to deal with cells that might
// use storage in their cell for the data, or an allocated stub (as all
// strings do).  Hence its concerns are a superset of those for strings.
//
// (We could just omit a TO handler here and it would fall through to
// the ANY-UTF8? generic, but this gives an opportunity to inject debug
// code and also to explain why there's not specific code for strings.)
//
IMPLEMENT_GENERIC(TO, Any_String)
{
    return GENERIC_CFUNC(TO, Any_Utf8)(LEVEL);
}


//
//  Alias_Any_String_As: C
//
// 1. The reason that strings have a different AS aliasing is that it keeps
//    the AS ANY-UTF8? generic from having to worry about converting types
//    with indices.
//
Result(Element*) Alias_Any_String_As(
    Sink(Element) out,
    const Element* string,
    Heart as
){
    if (Any_String_Type(as)) {  // special handling not in Utf8 generic [1]
        Copy_Cell(out, string);
        KIND_BYTE(out) = as;
        Inherit_Const(out, string);
        return out;
    }

    return Alias_Any_Utf8_As(out, string, as);
}


IMPLEMENT_GENERIC(AS, Any_String)
{
    INCLUDE_PARAMS_OF_AS;

    Element* string = Element_ARG(VALUE);
    Heart as = Datatype_Builtin_Heart(ARG(TYPE));

    require (
      Alias_Any_String_As(OUT, string, as)
    );
    return OUT;
}


IMPLEMENT_GENERIC(COPY, Any_String)
{
    INCLUDE_PARAMS_OF_COPY;

    Element* string = Element_ARG(VALUE);

    UNUSED(ARG(DEEP));  // :DEEP is historically ignored on ANY-STRING?

    REBINT len = Part_Len_May_Modify_Index(string, ARG(PART));

    require (
      Strand* copy = Copy_String_At_Limit(string, &len)
    );
    return Init_Any_String(
        OUT,
        Heart_Of_Builtin_Fundamental(string),
        copy
    );
}


IMPLEMENT_GENERIC(TAKE, Any_String)
{
    INCLUDE_PARAMS_OF_TAKE;

    Element* v = Element_ARG(SERIES);
    Ensure_Mutable(v);

    if (ARG(DEEP))
        panic (Error_Bad_Refines_Raw());

    REBLEN len;
    if (ARG(PART)) {
        len = Part_Len_May_Modify_Index(v, ARG(PART));
        if (len == 0) {
            Heart heart = Heart_Of_Builtin_Fundamental(v);
            require (
              Strand* strand = Make_Strand(0)
            );
            return Init_Any_String(OUT, heart, strand);
        }
    } else
        len = 1;

    // Note that :PART can change index

    REBLEN tail = Series_Len_Head(v);

    if (ARG(LAST)) {
        if (len > tail) {
            SERIES_INDEX_UNBOUNDED(v) = 0;
            len = tail;
        }
        else
            SERIES_INDEX_UNBOUNDED(v) = tail - len;
    }

    if (Series_Index(v) >= tail) {
        if (not ARG(PART))
            return fail (Error_Nothing_To_Take_Raw());
        Heart heart = Heart_Of_Builtin_Fundamental(v);
        require (
          Strand* strand = Make_Strand(0)
        );
        return Init_Any_String(OUT, heart, strand);
    }

    // if no :PART, just return value, else return string
    //
    if (ARG(PART)) {
        Heart heart = Heart_Of_Builtin_Fundamental(v);
        require (
          Strand* strand = Copy_String_At_Limit(v, &len)
        );
        Init_Any_String(OUT, heart, strand);
    }
    else
        Init_Char_Unchecked(OUT, Codepoint_At(String_At(v)));

    Remove_Any_Series_Len(v, Series_Index(v), len);
    return OUT;
}


IMPLEMENT_GENERIC(REVERSE, Any_String)
{
    INCLUDE_PARAMS_OF_REVERSE;

    Element* string = Element_ARG(SERIES);

    Strand* s = Cell_Strand_Ensure_Mutable(string);

    Copy_Cell(OUT, string);  // save before index adjustment
    REBINT len = Part_Len_May_Modify_Index(string, ARG(PART));
    Reverse_Strand(s, Series_Index(string), len);
    return OUT;
}


IMPLEMENT_GENERIC(RANDOM_PICK, Any_String)
{
    INCLUDE_PARAMS_OF_RANDOM_PICK;

    Element* v = Element_ARG(COLLECTION);

    Index index = Series_Index(v);
    REBLEN tail = Series_Len_Head(v);

    if (index >= tail)
        return fail (Error_Bad_Pick_Raw(Init_Integer(SPARE, 0)));

    index += Random_Int(did ARG(SECURE)) % (tail - index);

    return Init_Char_Unchecked(
        OUT,
        Get_Strand_Char_At(Cell_Strand(v), index)
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

    Element* string = Element_ARG(SERIES);

    Index index = Series_Index(string);

    Strand* s = Cell_Strand_Ensure_Mutable(string);

    if (not Is_Strand_All_Ascii(s))  // slow is better than not at all [1]
        return rebDelegate(
            "let shuffled: unspaced shuffle map-each 'c", string, "[c]"
            "take:part", string, "tail of", string,  // drop tail
            "append", string, "shuffled",  // add shuffled bit
            string  // return string at original position
        );

    bool secure = did ARG(SECURE);

    REBLEN n;
    for (n = Strand_Len(s) - index; n > 1;) {
        REBLEN k = index + (Random_Int(secure) % n);
        n--;
        Codepoint swap = Get_Strand_Char_At(s, k);
        require (
          Set_Char_At(s, k, Get_Strand_Char_At(s, n + index))
        );
        require (
          Set_Char_At(s, n + index, swap)
        );
    }
    return COPY(string);
}


IMPLEMENT_GENERIC(CODEPOINT_OF, Any_String)
{
    INCLUDE_PARAMS_OF_CODEPOINT_OF;

    Element* str = Element_ARG(VALUE);
    const Byte* bp = String_At(str);  // downgrade validated Utf8(*)

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
    return fail (Error_Not_One_Codepoint_Raw());
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

    Byte b1 = *cast(Byte*, v1);
    Byte b2 = *cast(Byte*, v2);

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
    Strand* str = Cell_Strand_Ensure_Mutable(v);  // just ensure mutability
    UNUSED(str);  // we use the Cell_Utf8_At() accessor, which is const

    if (ARG(ALL))
        panic (Error_Bad_Refines_Raw());

    if (ARG(COMPARE))
        panic (Error_Bad_Refines_Raw());  // !!! not in R3-Alpha

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
        panic ("Non-ASCII string sorting temporarily unavailable");

    REBLEN skip;
    if (not ARG(SKIP))
        skip = 1;
    else {
        skip = Get_Num_From_Arg(unwrap ARG(SKIP));
        if (skip <= 0 or len % skip != 0 or skip > len)
            panic (PARAM(SKIP));
    }

    // Use fast quicksort library function:
    REBLEN span = 1;
    if (skip > 1) {
        len /= skip;
        span *= skip;
    }

    Flags flags = 0;
    if (ARG(CASE))
        flags |= CC_FLAG_CASE;
    if (ARG(REVERSE))
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
//  encode-UTF-8: native [
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

    Stable* arg = ARG(ARG);

    if (Series_Len_At(ARG(OPTIONS)))
        panic ("UTF-8 Encoder Options not Designed Yet");

    Size utf8_size;
    Utf8(const*) utf8 = Cell_Utf8_Size_At(&utf8_size, arg);

    Binary* b = Make_Binary(utf8_size);
    memcpy(Binary_Head(b), cast(Byte*, utf8), utf8_size);
    Term_Binary_Len(b, utf8_size);
    return Init_Blob(OUT, b);
}


//
//  decode-UTF-8: native [
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

    if (Series_Len_At(ARG(OPTIONS)))
        panic ("UTF-8 Decoder Options not Designed Yet");

    Heart heart = TYPE_TEXT;  // should options let you specify? [1]

    Size size;
    const Byte* at = Blob_Size_At(&size, blob);
    return Init_Any_String(
        OUT,
        heart,
        Append_UTF8_May_Panic(nullptr, s_cast(at), size, STRMODE_NO_CR)
    );
}


//
//  Startup_String: C
//
void Startup_String(void)
{
    require (
      g_char_escapes = Alloc_N_On_Heap(Byte, MAX_ESC_CHAR + 1)
    );
    Mem_Fill(g_char_escapes, 0, MAX_ESC_CHAR + 1);

    Byte* cp = g_char_escapes;
    Byte c;
    for (c = '@'; c <= '_'; c++)
        *cp++ = c;

    g_char_escapes[cast(Byte, '\t')] = '-'; // tab
    g_char_escapes[cast(Byte, '\n')] = '/'; // line feed
    g_char_escapes[cast(Byte, '"')] = '"';
    g_char_escapes[cast(Byte, '^')] = '^';

    require (
      g_url_escapes = Alloc_N_On_Heap(Byte, MAX_URL_CHAR + 1)
    );
    Mem_Fill(g_url_escapes, 0, MAX_URL_CHAR + 1);

    for (c = 0; c <= ' '; c++)
        g_url_escapes[c] = ESC_URL | ESC_FILE;

    const Byte* dc = b_cast(";%\"()[]{}<>");

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


#if DEBUG_UTF8_EVERYWHERE

//
//  Verify_Strand_Length_Debug: C
//
void Verify_Strand_Length_Debug(const Strand* s) {
    if (Is_Strand_Symbol(s))
        return;  // no cached codepoint length for symbols

    Size size = Flex_Used(s);
    Length len = MISC_STRAND_NUM_CODEPOINTS(s);

    const Byte* tail = cast(Byte*, Strand_Head(s)) + size;

    Utf8(const*) check_cp = Strand_Head(s);
    REBLEN check_len = 0;
    for (; check_cp != tail; ++check_len)
        check_cp = Skip_Codepoint(check_cp);

    assert(check_len == len);
}

//
//  Verify_Strand_Bookmarks_Debug: C
//
void Verify_Strand_Bookmarks_Debug(const Strand* s) {
    if (Is_Strand_Symbol(s))
        return;  // no bookmarks for symbols

    BookmarkList* book = opt Link_Bookmarks(s);
    if (not book)
        return;  // no bookmarks

    Index index = BOOKMARK_INDEX(book);
    REBLEN offset = BOOKMARK_OFFSET(book);

    Utf8(const*) check_cp = Strand_Head(s);
    REBLEN check_index = 0;
    for (; check_index != index; ++check_index)
        check_cp = Skip_Codepoint(check_cp);
    assert(check_cp == cast(Byte*, Strand_Head(s)) + offset);
}

#endif
