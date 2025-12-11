//
//  file: %s-ops.c
//  summary: "string handling utilities"
//  section: strings
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


//
//  All_Bytes_ASCII: C
//
// Returns true if byte string does not use upper code page
// (e.g. no 128-255 characters)
//
bool All_Bytes_ASCII(Byte* bp, Size size)
{
    for (; size > 0; --size, ++bp)
        if (not Is_Byte_Ascii(*bp))
            return false;

    return true;
}


//
//  Trim_Tail: C
//
// Used to trim off hanging spaces during FORM and MOLD.
//
void Trim_Tail(Molder* mo, Byte ascii)
{
    assert(Is_Byte_Ascii(ascii));  // !!! work needed for multi-byte characters

    Length len = Strand_Len(mo->strand);
    Size size = Strand_Size(mo->strand);

    for (; size > 0; --size, --len) {
        Byte b = *Binary_At(mo->strand, size - 1);
        if (b != ascii)
            break;
    }

    Term_Strand_Len_Size(mo->strand, len, size);
}


//
//  Change_Case: C
//
// Common code for string case handling.
//
void Change_Case(
    Sink(Stable) out,
    Stable* val, // !!! Not const--uses Partial(), may change index, review
    Option(const Stable*) part,
    bool upper
){
    if (Is_Rune_And_Is_Char(val)) {
        Codepoint c = Rune_Known_Single_Codepoint(val);
        Init_Char_Unchecked(out, upper ? UP_CASE(c) : LO_CASE(c));
        return;
    }

    assert(Any_String(val));

    // This is a mutating operation, and we want to return the same series at
    // the same index.  However, R3-Alpha code would use Partial() and may
    // change val's index.  Capture it before potential change, review.
    //
    Copy_Cell(out, val);

    REBLEN len = Part_Len_May_Modify_Index(val, part);

    // !!! This assumes that all case changes will preserve the encoding size,
    // but that's not true (some strange multibyte accented characters have
    // capital or lowercase versions that are single byte).  This may be
    // uncommon enough to have special handling (only do something weird, e.g.
    // use the mold buffer, if it happens...for the remaining portion of such
    // a string...and only if the size *expands*).  Expansions also may never
    // be possible, only contractions (is that true?)  Review when UTF-8
    // Everywhere is more mature to the point this is worth worrying about.
    //
    Utf8(*) up = String_At_Ensure_Mutable(val);
    Utf8(*) dp;
    if (upper) {
        REBLEN n;
        for (n = 0; n < len; n++) {
            dp = up;

            Codepoint c;
            up = Utf8_Next(&c, up);
            if (c < NUM_UNICODE_CASES) {
                dp = Write_Codepoint(dp, UP_CASE(c));
                assert(dp == up); // !!! not all case changes same byte size?
            }
        }
    }
    else {
        REBLEN n;
        for (n = 0; n < len; n++) {
            dp = up;

            Codepoint c;
            up = Utf8_Next(&c, up);
            if (c < NUM_UNICODE_CASES) {
                dp = Write_Codepoint(dp, LO_CASE(c));
                assert(dp == up); // !!! not all case changes same byte size?
            }
        }
    }
}


//
//  Split_Lines: C
//
// Given a string series, split lines on CR-LF.  Give back array of strings.
//
// Note: The definition of "line" in POSIX is a sequence of characters that
// end with a newline.  Hence, the last line of a file should have a newline
// marker, or it's not a "line")
//
// https://stackoverflow.com/a/729795
//
// This routine does not require it.
//
// !!! CR support is likely to be removed...and CR will be handled as a normal
// character, with special code needed to process it.
//
Source* Split_Lines(const Element* str)
{
    StackIndex base = TOP_INDEX;

    Length len = Series_Len_At(str);
    REBLEN i = Series_Index(str);
    if (i == len)
        return Make_Source(0);

    DECLARE_MOLDER (mo);
    Push_Mold(mo);

    Utf8(const*) cp = String_At(str);

    Codepoint c;
    cp = Utf8_Next(&c, cp);

    for (; i < len; ++i, cp = Utf8_Next(&c, cp)) {
        if (c != LF && c != CR) {
            Append_Codepoint(mo->strand, c);
            continue;
        }

        Init_Text(PUSH(), Pop_Molded_Strand(mo));
        Set_Cell_Flag(TOP, NEWLINE_BEFORE);

        Push_Mold(mo);

        if (c == CR) {
            Utf8(const*) tp = Utf8_Next(&c, cp);
            if (c == LF) {
                ++i;
                cp = tp; // treat CR LF as LF, lone CR as LF
            }
        }
    }

    // If there's any remainder we pushed in the buffer, consider the end of
    // string to be an implicit line-break

    if (Strand_Size(mo->strand) == mo->base.size)
        Drop_Mold(mo);
    else {
        Init_Text(PUSH(), Pop_Molded_Strand(mo));
        Set_Cell_Flag(TOP, NEWLINE_BEFORE);
    }

    Source* a = Pop_Source_From_Stack(base);
    Set_Source_Flag(a, NEWLINE_AT_TAIL);
    return a;
}
