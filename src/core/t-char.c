//
//  File: %t-char.c
//  Summary: "character datatype"
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
// See %sys-char.h for notes.

#include "sys-core.h"


// Index into the table below with the first byte of a UTF-8 sequence to
// get the number of trailing bytes that are supposed to follow it.
// Note that *legal* UTF-8 values can't have 4 or 5-bytes. The table is
// left as-is for anyone who may want to do such conversion, which was
// allowed in earlier algorithms.
//
const char g_trailing_bytes_for_utf8[256] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, 3,3,3,3,3,3,3,3,4,4,4,4,5,5,5,5
};


// Magic values subtracted from a buffer value during UTF8 conversion.
// This table contains as many values as there might be trailing bytes
// in a UTF-8 sequence.
//
const uint_fast32_t g_offsets_from_utf8[6] = {
    0x00000000UL, 0x00003080UL, 0x000E2080UL,
    0x03C82080UL, 0xFA082080UL, 0x82082080UL
};


// Once the bits are split out into bytes of UTF-8, this is a mask OR-ed
// into the first byte, depending on how many bytes follow.  There are
// as many entries in this table as there are UTF-8 sequence types.
// (I.e., one byte sequence, two byte... etc.). Remember that sequencs
// for *legal* UTF-8 will be 4 or fewer bytes total.
//
const uint_fast8_t g_first_byte_mark_utf8[7] = {
    0x00, 0x00, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC
};


//
//  CT_Issue: C
//
// As the replacement for CHAR!, ISSUE! inherits the behavior that there are
// no non-strict comparisons.  To compare non-strictly, they must be aliased
// as TEXT!.
//
REBINT CT_Issue(const Cell* a, const Cell* b, bool strict)
{
    UNUSED(strict);  // always strict

    if (IS_CHAR_CELL(a) and IS_CHAR_CELL(b)) {
        REBINT num = Cell_Codepoint(a) - Cell_Codepoint(b);
        if (num == 0)
            return 0;
        return (num > 0) ? 1 : -1;
    }
    else if (not IS_CHAR_CELL(a) and not IS_CHAR_CELL(b))
        return CT_String(a, b, true);  // strict=true
    else
        return IS_CHAR_CELL(a) ? -1 : 1;
}


//
//  MAKE_Issue: C
//
Bounce MAKE_Issue(
    Level* level_,
    Kind k,
    Option(const Value*) parent,
    const REBVAL *arg
){
    assert(k == REB_ISSUE);
    UNUSED(k);

    if (parent)
        fail (Error_Bad_Make_Parent(REB_ISSUE, unwrap(parent)));

    switch(VAL_TYPE(arg)) {
      case REB_INTEGER:
      case REB_DECIMAL: {
        REBINT n = Int32(arg);
        Context* error = Maybe_Init_Char(OUT, n);
        if (error)
            return RAISE(error);
        return OUT; }

      case REB_BINARY: {
        Size size;
        const Byte* bp = Cell_Binary_Size_At(&size, arg);
        if (size == 0)
            goto bad_make;

        Codepoint c;
        if (*bp <= 0x80) {
            if (size != 1)
                return MAKE_String(level_, REB_ISSUE, nullptr, arg);

            c = *bp;
        }
        else {
            bp = Back_Scan_UTF8_Char(&c, bp, &size);
            --size;  // must decrement *after* (or Back_Scan() will fail)
            if (bp == nullptr)
                goto bad_make;  // must be valid UTF8
            if (size != 0)
                return MAKE_String(level_, REB_ISSUE, nullptr, arg);
        }
        Context* error = Maybe_Init_Char(OUT, c);
        if (error)
            return RAISE(error);
        return OUT; }

      case REB_TEXT:
        if (Cell_Series_Len_At(arg) == 0)
            fail ("Empty ISSUE! is zero codepoint, unlike empty TEXT!");
        if (Cell_Series_Len_At(arg) == 1)
            return Init_Char_Unchecked(OUT, Codepoint_At(Cell_Utf8_At(arg)));
        return MAKE_String(level_, REB_ISSUE, nullptr, arg);

      default:
        break;
    }

  bad_make:

    return RAISE(Error_Bad_Make(REB_ISSUE, arg));
}


//
//  codepoint-to-char: native [
//
//  "Make a character out of an integer codepoint"
//
//      return: [char?]
//      codepoint [integer!]
//  ]
//
DECLARE_NATIVE(codepoint_to_char)
{
    INCLUDE_PARAMS_OF_CODEPOINT_TO_CHAR;

    uint32_t c = VAL_UINT32(ARG(codepoint));

    Maybe_Init_Char(OUT, c);
    return OUT;
}


//
//  utf8-to-char: native [
//
//  "Make a single character out of a UTF-8 binary sequence"
//
//      return: [char?]
//      utf8 [binary!]
//  ]
//
DECLARE_NATIVE(utf8_to_char)
{
    INCLUDE_PARAMS_OF_UTF8_TO_CHAR;

    Size size;
    const Byte *encoded = Cell_Binary_Size_At(&size, ARG(utf8));

    if (size == 0)
        fail ("Empty binary passed to UTF8-TO-CHAR");

    Codepoint c;
    if (nullptr == Back_Scan_UTF8_Char(&c, encoded, &size))
        fail ("Invalid UTF-8 Sequence found in UTF8-TO-CHAR");

    assert(size != 0);  // Back_Scan() assumes one byte decrement

    if (size != 1)
        fail ("More than one codepoint found in UTF8-TO-CHAR conversion");

    Init_Char_Unchecked(OUT, c);  // !!! Guaranteed good character?
    return OUT;
}


//
//  TO_Issue: C
//
// General semantics of TO and MAKE have been historically confusing, and
// are further complicated by CHAR! no longer being a unique fundamental
// type (but rather a single-codepoint form of ISSUE!).  Functionality is
// divided into functions like CODEPOINT-TO-CHAR and UTF8-TO-CHAR, which
// leave things like TO ISSUE! 10 to be #10.
//
Bounce TO_Issue(Level* level_, Kind kind, const REBVAL *arg)
{
    assert(VAL_TYPE(arg) != REB_ISSUE);  // !!! should call COPY?

    if (Any_String(arg) or Any_Word(arg)) {
        Length len;
        Size size;
        Utf8(const*) utf8 = Cell_Utf8_Len_Size_At(&len, &size, arg);

        if (len == 0)  // don't "accidentally" create zero-codepoint `#`
            return RAISE(Error_Illegal_Zero_Byte_Raw());

        return Init_Issue_Utf8(OUT, utf8, size, len);
    }

    return RAISE(Error_Bad_Cast_Raw(arg, Datatype_From_Kind(kind)));
}


static REBINT Math_Arg_For_Char(REBVAL *arg, const Symbol* verb)
{
    switch (VAL_TYPE(arg)) {
      case REB_ISSUE:
        return Cell_Codepoint(arg);

      case REB_INTEGER:
        return VAL_INT32(arg);

      case REB_DECIMAL:
        return cast(REBINT, VAL_DECIMAL(arg));

      default:
        fail (Error_Math_Args(REB_ISSUE, verb));
    }
}


//
//  MF_Sigil: C
//
void MF_Sigil(REB_MOLD *mo, const Cell* v, bool form)
{
    UNUSED(form);

    const char* utf8 = cs_cast(PAYLOAD(Bytes, v).at_least_8);
    Size size = EXTRA(Bytes, v).exactly_4[IDX_EXTRA_USED];
    Append_Utf8(mo->series, utf8, size);
}


//
//  MF_Issue: C
//
void MF_Issue(REB_MOLD *mo, const Cell* v, bool form)
{
    if (form) {
        if (IS_CHAR_CELL(v) and Cell_Codepoint(v) == 0)
            fail (Error_Illegal_Zero_Byte_Raw());  // don't form #, only mold

        Append_String_Limit(mo->series, v, UNLIMITED);
        return;
    }

    Length len;
    Utf8(const*) cp = Cell_Utf8_Len_Size_At(&len, nullptr, v);

    Append_Codepoint(mo->series, '#');

    if (len == 0)
        return;  // Just be `#`

    // !!! This should be smarter and share code with FILE! on whether
    // it's necessary to use double quotes or braces, and how escaping
    // should be done.  For now, just do a simple scan to get the gist
    // of what that logic *should* do.

    bool no_quotes = true;
    Codepoint c = Codepoint_At(cp);
    for (; c != '\0'; cp = Utf8_Next(&c, cp)) {
        if (
            c <= 32  // control codes up to 32 (space)
            or (
                c >= 127  // 127 is delete, begins more control codes
                and c <= 160  // 160 is non-breaking space, 161 starts Latin1
            )
        ){
            no_quotes = false;
            break;
        }
        if (IS_LEX_DELIMIT(c) and IS_LEX_DELIMIT_HARD(c)) {
            no_quotes = false;  // comma, bracket, parentheses...
            break;
        }
    }

    if (no_quotes or Not_Cell_Flag(v, STRINGLIKE_HAS_NODE)) {  // !!! hack
        if (len == 1 and not no_quotes) {  // use historical CHAR! molding
            bool parened = GET_MOLD_FLAG(mo, MOLD_FLAG_ALL);

            Append_Codepoint(mo->series, '"');
            Mold_Uni_Char(mo, Cell_Codepoint(v), parened);
            Append_Codepoint(mo->series, '"');
        }
        else
            Append_String_Limit(mo->series, v, len);
    }
    else {
        const String* s = Cell_Issue_String(v);  // !!! needs node
        Mold_Text_Series_At(mo, s, 0);
    }
}


//
//  REBTYPE: C
//
REBTYPE(Issue)
{
    REBVAL *issue = D_ARG(1);

    Option(SymId) sym = Symbol_Id(verb);

    switch (sym) {
      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;
        UNUSED(ARG(value));  // same as `v`

        switch (Cell_Word_Id(ARG(property))) {
          case SYM_CODEPOINT:
            if (not IS_CHAR(issue))
                break;  // must be a single codepoint to use this reflector
            return Init_Integer(OUT, Cell_Codepoint(issue));

          case SYM_SIZE: {
            Size size;
            Cell_Utf8_Size_At(&size, issue);
            return Init_Integer(OUT, size); }

          case SYM_LENGTH: {
            REBLEN len;
            Cell_Utf8_Len_Size_At(&len, nullptr, issue);
            return Init_Integer(OUT, len); }

          default:
            break;
        }
        fail (PARAM(property)); }

      case SYM_COPY:  // since copy result is also immutable, Move() suffices
        return Copy_Cell(OUT, issue);

      default:
        break;
    }

    // !!! All the math operations below are inherited from the CHAR!
    // implementation, and will not work if the ISSUE! length is > 1.
    //
    if (not IS_CHAR(issue))
        fail ("Math operations only usable on single-character ISSUE!");

    // Don't use a Codepoint for chr, because it does signed math and then will
    // detect overflow.
    //
    REBI64 chr = cast(REBI64, Cell_Codepoint(issue));
    REBI64 arg;

    switch (sym) {
      case SYM_PICK_P: {
        INCLUDE_PARAMS_OF_PICK_P;
        UNUSED(ARG(location));

        const Value* picker = ARG(picker);
        if (not Is_Integer(picker))
            fail (PARAM(picker));

        REBI64 n = VAL_INT64(picker);
        if (n <= 0)
            return nullptr;

        REBLEN len;
        Utf8(const*) cp = Cell_Utf8_Len_Size_At(&len, nullptr, issue);
        if (cast(REBLEN, n) > len)
            return nullptr;

        Codepoint c;
        cp = Utf8_Next(&c, cp);
        for (; n != 1; --n)
            cp = Utf8_Next(&c, cp);

        return Init_Integer(OUT, c); }

      case SYM_ADD: {
        arg = Math_Arg_For_Char(D_ARG(2), verb);
        chr += arg;
        break; }

      case SYM_SUBTRACT: {
        arg = Math_Arg_For_Char(D_ARG(2), verb);

        // Rebol2 and Red return CHAR! values for subtraction from another
        // CHAR! (though Red checks for overflow and errors on something like
        // `subtract #"^(00)" #"^(01)"`, vs returning #"^(FF)").
        //
        // R3-Alpha chose to return INTEGER! and gave a signed difference, so
        // the above would give -1.
        //
        if (IS_CHAR(D_ARG(2))) {
            Init_Integer(OUT, chr - arg);
            return OUT;
        }

        chr -= arg;
        break; }

      case SYM_MULTIPLY:
        arg = Math_Arg_For_Char(D_ARG(2), verb);
        chr *= arg;
        break;

      case SYM_DIVIDE:
        arg = Math_Arg_For_Char(D_ARG(2), verb);
        if (arg == 0)
            fail (Error_Zero_Divide_Raw());
        chr /= arg;
        break;

      case SYM_REMAINDER:
        arg = Math_Arg_For_Char(D_ARG(2), verb);
        if (arg == 0)
            fail (Error_Zero_Divide_Raw());
        chr %= arg;
        break;

      case SYM_BITWISE_NOT:
        chr = cast(Codepoint, ~chr);
        break;

      case SYM_BITWISE_AND:
        arg = Math_Arg_For_Char(D_ARG(2), verb);
        chr &= cast(Codepoint, arg);
        break;

      case SYM_BITWISE_OR:
        arg = Math_Arg_For_Char(D_ARG(2), verb);
        chr |= cast(Codepoint, arg);
        break;

      case SYM_BITWISE_XOR:
        arg = Math_Arg_For_Char(D_ARG(2), verb);
        chr ^= cast(Codepoint, arg);
        break;

      case SYM_BITWISE_AND_NOT:
        arg = Math_Arg_For_Char(D_ARG(2), verb);
        chr &= cast(Codepoint, ~arg);
        break;

      case SYM_EVEN_Q:
        return Init_Logic(OUT, did (cast(Codepoint, ~chr) & 1));

      case SYM_ODD_Q:
        return Init_Logic(OUT, did (chr & 1));

      case SYM_RANDOM: {
        INCLUDE_PARAMS_OF_RANDOM;

        UNUSED(PARAM(value));
        if (REF(only))
            fail (Error_Bad_Refines_Raw());

        if (REF(seed)) {
            Set_Random(chr);
            return TRASH;
        }
        if (chr == 0)
            break;
        chr = cast(Codepoint,
            1 + cast(REBLEN, Random_Int(REF(secure)) % chr)
        );
        break; }

      default:
        fail (UNHANDLED);
    }

    if (chr < 0)
        return RAISE(Error_Type_Limit_Raw(Datatype_From_Kind(REB_ISSUE)));

    Context* error = Maybe_Init_Char(OUT, cast(Codepoint, chr));
    if (error)
        return RAISE(error);
    return OUT;
}


//
//  trailing-bytes-for-utf8: native [
//
//  "Given the first byte of a UTF-8 encoding, how many bytes should follow"
//
//      return: [integer!]
//      first-byte [integer!]
//      /extended "Permit 4 or 5 trailing bytes, not legal in the UTF-8 spec"
//  ]
//
DECLARE_NATIVE(trailing_bytes_for_utf8)
//
// !!! This is knowledge Rebol has, and it can be useful for anyone writing
// code that processes UTF-8 (e.g. the terminal).  Might as well expose it.
{
    INCLUDE_PARAMS_OF_TRAILING_BYTES_FOR_UTF8;

    REBINT byte = VAL_INT32(ARG(first_byte));
    if (byte < 0 or byte > 255)
        fail (Error_Out_Of_Range(ARG(first_byte)));

    uint_fast8_t trail = g_trailing_bytes_for_utf8[cast(Byte, byte)];
    if (trail > 3 and not REF(extended)) {
        assert(trail == 4 or trail == 5);
        fail ("Use /EXTENDED with TRAILNG-BYTES-FOR-UTF-8 for 4 or 5 bytes");
    }

    return Init_Integer(OUT, trail);
}
