//
//  file: %s-make.c
//  summary: "binary and unicode string support"
//  section: strings
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2024 Ren-C Open Source Contributors
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



//
//  Make_Strand_Core: C
//
// Makes a Flex to hold a String with enough capacity for a certain amount
// of encoded data.  Note that this is not a guarantee of being able to hold
// more than `encoded_capacity / UNI_ENCODED_MAX` unencoded codepoints...
//
Result(Strand*) Make_Strand_Core(Flags flags, Size encoded_capacity)
{
    assert(Flavor_From_Flags(flags) == FLAVOR_NONSYMBOL);

    trap (
      Strand* str = nocast Make_Flex(
        STUB_MASK_STRAND | flags,
        encoded_capacity + 1  // + 1 makes room for '\0' terminator
    ));
    Tweak_Misc_Num_Codepoints(str, 0);
    Tweak_Link_Bookmarks(str, nullptr);  // generated on demand
    *Binary_Head(str) = '\0';  // zero length, so head = tail
    return str;
}


//
//  Make_Binary_From_Sized_Bytes: C
//
// Create a Binary Flex from the given bytes.
//
Binary* Make_Binary_From_Sized_Bytes(const Byte* src, Size len)
{
    Binary* b = Make_Binary(len);
    memcpy(Binary_Head(b), src, len);
    Term_Binary_Len(b, len);
    return b;
}


//
//  Copy_String_At_Limit: C
//
// Copying a Strand is distinct from copying a Binary due to the length being
// counted in characters, and not units of the Flex width (1).
//
Result(Strand*) Copy_String_At_Limit(
    const Cell* src,
    Option(const Length*) limit
){
    Size limited_size;
    Length limited_length;
    Utf8(const*) utf8 = Cell_Utf8_Len_Size_At_Limit(
        &limited_length,
        &limited_size,
        src,
        limit
    );

    trap (
      Strand* dst = Make_Strand(limited_size)
    );
    memcpy(cast(Byte*, Strand_Head(dst)), cast(Byte*, utf8), limited_size);
    Term_Strand_Len_Size(dst, limited_length, limited_size);

    return dst;
}


//
//  Append_Codepoint: C
//
// Encode a codepoint onto the end of a UTF-8 String Flex.  This is used
// frequently by molding.
//
// !!! Should the mold buffer avoid paying for termination?  Might one save on
// resizing checks if an invalid UTF-8 byte were used to mark the end of the
// capacity (the way END markers are used on the data stack?)
//
Strand* Append_Codepoint(Strand* dst, Codepoint c)
{
    if (c == '\0') {
        assert(!"Zero byte being added to string.");  // caller should handle
        panic (Error_Illegal_Zero_Byte_Raw());  // don't crash release build
    }

    assert(c <= MAX_UNI);
    assert(not Is_Strand_Symbol(dst));

    Length old_len = Strand_Len(dst);

    Size tail = Strand_Size(dst);
    Size encoded_size = Encoded_Size_For_Codepoint(c);
    require (
      Expand_Flex_Tail_And_Update_Used(dst, encoded_size)
    );
    Encode_UTF8_Char(Binary_At(dst, tail), c, encoded_size);

    // "length" grew by 1 codepoint, but "size" grew by 1 to UNI_MAX_ENCODED
    //
    Term_Strand_Len_Size(dst, old_len + 1, tail + encoded_size);

    return dst;
}


//
//  Make_Codepoint_Strand: C
//
// Create a string that holds a single codepoint.
//
// !!! This could be more optimal if a CHAR! is passed in, because it caches
// the UTF-8 encoding in the cell.  Review callsites if that is actionable.
//
Result(Strand*) Make_Codepoint_Strand(Codepoint c)
{
    if (c == '\0')
        return fail (Error_Illegal_Zero_Byte_Raw());

    Size size = Encoded_Size_For_Codepoint(c);
    trap (
      Strand* s = Make_Strand(size)
    );
    Encode_UTF8_Char(Strand_Head(s), c, size);
    Term_Strand_Len_Size(s, 1, size);
    return s;
}


//
//  Append_Ascii_Len: C
//
// Append unencoded data to a String, using plain memcpy().  If dst is
// nullptr, a new String will be created and returned.
//
// !!! Should checked build assert it's ASCII?  Most of these are coming from
// C literals in the source.
//
Result(Strand*) Append_Ascii_Len(Strand* dst, const char *ascii, REBLEN len)
{
    REBLEN old_size;
    REBLEN old_len;

    if (dst == nullptr) {
        trap (
          dst = Make_Strand(len)
        );
        old_size = 0;
        old_len = 0;
    }
    else {
        old_size = Strand_Size(dst);
        old_len = Strand_Len(dst);
        trap (
          Expand_Flex_Tail_And_Update_Used(dst, len)
        );
    }

    memcpy(Binary_At(dst, old_size), ascii, len);

    Term_Strand_Len_Size(dst, old_len + len, old_size + len);
    return dst;
}


//
//  Append_Ascii: C
//
// Append_Ascii_Len() variant that looks for a terminating 0 byte to
// determine the length.  Assumes one byte per character.
//
// !!! Should be in a header file so it can be inlined.
//
Result(Strand*) Append_Ascii(Strand* dst, const char *src)
{
    return Append_Ascii_Len(dst, src, strsize(src));
}


//
//  Append_Utf8: C
//
// Append validated UTF-8 bytes to a String Flex.  Terminates.
//
void Append_Utf8(Strand* dst, Utf8(const*) utf8, Length len, Size size)
{
    Length old_len = Strand_Len(dst);
    Size old_used = Strand_Size(dst);

    REBLEN tail = Strand_Size(dst);
    require (
      Expand_Flex_At_Index_And_Update_Used(dst, tail, size)
    );

    memcpy(Binary_At(dst, tail), cast(Byte*, utf8), size);
    Term_Strand_Len_Size(dst, old_len + len, old_used + size);
}


//
//  Append_Spelling: C
//
// Append the spelling of a REBSTR to a UTF8 binary.  Terminates.
//
void Append_Spelling(Strand* dst, const Symbol* s)
{
    Utf8(const*) utf8 = Strand_Head(s);
    Length len = Strand_Len(s);
    Size size = Strand_Size(s);
    Append_Utf8(dst, utf8, len, size);
}


//
//  Append_Any_Utf8_Limit: C
//
// Append a partial string to a Strand*.
//
void Append_Any_Utf8_Limit(
    Strand* dst,
    const Cell* src,
    Option(const Length*) limit
){
    assert(not Is_Flex_Frozen(dst));
    assert(Any_Utf8_Type(Heart_Of(src)));

    Length len;
    Size size;
    Utf8(const*) utf8 = Cell_Utf8_Len_Size_At_Limit(&len, &size, src, limit);
    Append_Utf8(dst, utf8, len, size);
}


//
//  Append_Int: C
//
// Append an integer string.
//
Result(Zero) Append_Int(Strand* dst, REBINT num)
{
    Byte buf[32];
    Form_Int(buf, num);

    trap (
      Append_Ascii(dst, s_cast(buf))
    );
    return zero;
}


//
//  Append_Int_Pad: C
//
// Append an integer string.
//
Result(Zero) Append_Int_Pad(Strand* dst, REBINT num, REBINT digs)
{
    Byte buf[32];
    if (digs > 0)
        Form_Int_Pad(buf, num, digs, -digs, '0');
    else
        Form_Int_Pad(buf, num, -digs, digs, '0');

    trap (
      Append_Ascii(dst, s_cast(buf))
    );
    return zero;
}



//
//  Append_UTF8_May_Panic: C
//
// Append UTF-8 data to a String Flex (or create new one)
//
// This routine does not just append bytes blindly because:
//
// * If STRMODE_CRLF_TO_LF is set, some characters may need to be removed
//
// * We want to check for invalid byte sequences, as this can be called with
//   arbitrary outside data from the API.
//
// * It's needed to know how many characters (length) are in the String, not
//   just how many bytes.  The higher level concept of "length" gets stored
//   in String.misc.num_codepoints
//
Strand* Append_UTF8_May_Panic(
    Strand* dst,  // if nullptr, that means make a new string
    const char *utf8,
    Size size,
    enum Reb_Strmode strmode
){
    const Byte* bp = b_cast(utf8);

    DECLARE_MOLDER (mo); // !!! REVIEW: don't need intermediate if no CRLF_TO_LF
    Push_Mold(mo);

    Length num_codepoints = 0;

    Size bytes_left = size;  // see remarks on Back_Scan_Utf8_Char's 3rd arg
    for (; bytes_left > 0; --bytes_left, ++bp) {
        Codepoint c = *bp;
        if (Is_Utf8_Lead_Byte(c)) {
            c = Back_Scan_Utf8_Char(&bp, &bytes_left) except (Error* e) {
                panic (e);
            }
        }
        else if (Should_Skip_Ascii_Byte_May_Panic(
            bp,
            strmode,
            cast(Byte*, utf8)
        )){
            continue;
        }

        ++num_codepoints;
        Append_Codepoint(mo->strand, c);
    }

    // !!! The implicit nature of this is probably not the best way of
    // handling things, but... if the String we were supposed to be appending
    // to was the mold buffer, that's what we just did.  Consider making this
    // a specific call for Mold_Utf8() or similar.
    //
    if (dst == mo->strand)
        return dst;

    if (not dst)
        return Pop_Molded_Strand(mo);

    Length old_len = Strand_Len(dst);
    Size old_size = Strand_Size(dst);

    require (
      Expand_Flex_Tail_And_Update_Used(dst, size)
    );
    memcpy(
        Binary_At(dst, old_size),
        Binary_At(mo->strand, mo->base.size),
        Strand_Size(mo->strand) - mo->base.size
    );

    Term_Strand_Len_Size(
        dst,
        old_len + num_codepoints,
        old_size + Strand_Size(mo->strand) - mo->base.size
    );

    Drop_Mold(mo);

    return dst;
}


//
//  Join_Binary_In_Byte_Buf: C
//
// Join a binary from component values for use in standard
// actions like make, insert, or append.
// limit: maximum number of values to process
// limit < 0 means no limit
//
// !!! This routine uses a different buffer from molding, because molding
// currently has to maintain valid UTF-8 data.  It may be that the buffers
// should be unified.
//
void Join_Binary_In_Byte_Buf(const Value* blk, REBINT limit)
{
    Binary* buf = BYTE_BUF;

    REBLEN tail = 0;

    if (limit < 0)
        limit = Series_Len_At(blk);

    Set_Flex_Len(buf, 0);

    const Element* val = List_Item_At(blk);
    for (; limit > 0; val++, limit--) {
        switch (maybe Type_Of(val)) {
          case TYPE_QUASIFORM:
            panic (Error_Bad_Value(val));

          case TYPE_INTEGER:
            require (
              Expand_Flex_Tail_And_Update_Used(buf, 1)
            );
            *Binary_At(buf, tail) = cast(Byte, VAL_UINT8(val));  // can panic()
            break;

          case TYPE_BLOB: {
            Size size;
            const Byte* data = Blob_Size_At(&size, val);
            require (
              Expand_Flex_Tail_And_Update_Used(buf, size)
            );
            memcpy(Binary_At(buf, tail), data, size);
            break; }

          case TYPE_RUNE:
          case TYPE_TEXT:
          case TYPE_FILE:
          case TYPE_EMAIL:
          case TYPE_URL:
          case TYPE_TAG: {
            Size utf8_size;
            Utf8(const*) utf8 = Cell_Utf8_Size_At(&utf8_size, val);

            require (
              Expand_Flex_Tail_And_Update_Used(buf, utf8_size)
            );
            memcpy(Binary_At(buf, tail), cast(Byte*, utf8), utf8_size);
            Set_Flex_Len(buf, tail + utf8_size);
            break; }

          default:
            panic (Error_Bad_Value(val));
        }

        tail = Flex_Used(buf);
    }

    *Binary_At(buf, tail) = 0;
}
