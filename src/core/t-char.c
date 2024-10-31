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
//  Trap_Back_Scan_Utf8_Char: C
//
// Decodes a single encoded UTF-8 codepoint and updates the position *at the
// the last byte of the character's data*.  (This differs from the usual
// `Scan_XXX` interface of returning the position after the scanned element,
// ready to read the next one.)
//
// The peculiar interface is useful in loops that process ordinary ASCII chars
// directly -as well- as UTF-8 ones.  The loop can do a single byte pointer
// increment after both kinds of elements, avoiding the need to call any kind
// of `Scan_Ascii()`:
//
//     for (; size > 0; ++bp, --size) {
//         if (*bp < 0x80) {
//             // do ASCII stuff...
//         }
//         else {
//             Codepoint uni;
//             Option(Error*) e = Trap_Back_Scan_Utf8_Char(&uni, &bp, &size);
//             if (e) { /* handle error */ }
//             // do UNICODE stuff...
//         }
//     }
//
// The third parameter is an optional size that will be decremented by
// the number of "extra" bytes the UTF8 has beyond a single byte character.
// This allows for decrement-style loops such as the above.
//
// If failure due to insufficient data or malformed bytes, then an error is
// returned (size is not advanced).
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// 1. Note that Ren-C disallows internal zero bytes in ANY-STRING?, so that
//    a single pointer can be given to C for the data in APIs like rebText(),
//    with no length...and not have this be misleading or cause bugs.  Same
//    for getting back a single pointer from rebSpell() for the data and
//    not be missing some part of it.
//
// 2. This check was considered "too expensive" and omitted in R3-Alpha:
//
//      https://github.com/rebol/rebol-issues/issues/638
//      https://en.wikipedia.org/wiki/UTF-8#Overlong_encodings
//
//    ...which meant that various illegal input patterns would be tolerated,
//    so long as they didn't cause crashes.  You would just not have the
//    input validated, and get garbage characters out.  The Ren-C philosophy
//    is that since this check only applies to non-ASCII, it is worth it to
//    do the validation.  And it only applies when scanning strings...once
//    they are loaded into String* we use Back_Scan_Utf8_Char_Unchecked().
//
// 3. We want the erroring cases to be inexpensive, because UTF-8 characters
//    are scanned for instance in FIND of a TEXT! in a binary BLOB! which may
//    have lots of invalid UTF-8.  So all the errors used here are
//    pre-allocated.  But those allocations only happen once the error
//    machinery is ready.
//
Option(Error*) Trap_Back_Scan_Utf8_Char(
    Sink(Codepoint) out,  // valid codepoint, no NUL or substitution chars [1]
    const Byte** bp,  // left alone if error result, "back updated" if not
    Option(Need(Size*)) size  // decremented in non-error case
){
    Codepoint c = 0;

    const Byte* source = *bp;
    uint_fast8_t trail = g_trailing_bytes_for_utf8[*source];

    if (size) {  // Check that we have enough valid source bytes
        if (cast(uint_fast8_t, trail + 1) > *(unwrap size))
            return Cell_Error(g_error_utf8_too_short);  // cached [3]
    }
    else if (trail != 0) {
        do {
            if (source[trail] < 0x80)
                return Cell_Error(g_error_utf8_trail_bad_bit);  // cached [3]
        } while (--trail != 0);

        trail = g_trailing_bytes_for_utf8[*source];
    }

    if (not Is_Legal_UTF8(source, trail + 1))  // was omitted in R3-Alpha [2]
        return Cell_Error(g_error_overlong_utf8);  // cached [3]

    switch (trail) {
        case 5: c += *source++; c <<= 6;  // falls through
        case 4: c += *source++; c <<= 6;  // falls through
        case 3: c += *source++; c <<= 6;  // falls through
        case 2: c += *source++; c <<= 6;  // falls through
        case 1: c += *source++; c <<= 6;  // falls through
        case 0: c += *source++;
    }
    c -= g_offsets_from_utf8[trail];

    if (c > UNI_MAX_LEGAL_UTF32)
        return Cell_Error(g_error_codepoint_too_high);  // cached [3]
    if (c >= UNI_SUR_HIGH_START and c <= UNI_SUR_LOW_END)
        return Cell_Error(g_error_no_utf8_surrogates);  // cached [3]

    if (size)
        *(unwrap size) -= trail;

    if (c == 0)  // string types disallow internal 0 bytes in Ren-C [1]
        return Cell_Error(g_error_illegal_zero_byte);  // cached [3]

    *out = c;
    *bp += trail;
    return nullptr;  // no error to return, success!
}


//
//  CT_Utf8: C
//
// As the replacement for CHAR!, ISSUE! inherits the behavior that there are
// no non-strict comparisons.  To compare non-strictly, they must be aliased
// as TEXT!.
//
REBINT CT_Utf8(const Cell* a, const Cell* b, bool strict)
{
    UNUSED(strict);  // always strict

    if (IS_CHAR_CELL(a) and IS_CHAR_CELL(b)) {
        Codepoint ca = Cell_Codepoint(a);
        Codepoint cb = Cell_Codepoint(b);
        REBINT num = Cast_Signed(ca) - Cast_Signed(cb);
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
//  Makehook_Utf8: C
//
Bounce Makehook_Utf8(Level* level_, Kind k, Element* arg) {
    assert(Any_Utf8_Kind(k));

    switch(VAL_TYPE(arg)) {
      case REB_INTEGER: {
        if (k != REB_ISSUE)
            fail ("Only ISSUE! can MAKE a UTF-8 immutable type with INTEGER!");

        REBINT n = Int32(arg);
        Option(Error*) error = Trap_Init_Char(OUT, n);
        if (error)
            return RAISE(unwrap error);
        return OUT; }

      case REB_BINARY: {
        if (k != REB_ISSUE)
            fail ("Only ISSUE! can MAKE a UTF-8 immutable type with BINARY!");

        Size size;
        const Byte* bp = Cell_Binary_Size_At(&size, arg);
        if (size == 0)
            goto bad_make;

        Codepoint c;
        if (*bp <= 0x80) {
            if (size != 1)
                return Makehook_String(level_, REB_ISSUE, arg);

            c = *bp;
        }
        else {
            Option(Error*) e = Trap_Back_Scan_Utf8_Char(&c, &bp, &size);
            if (e)
                return RAISE(unwrap e);  // must be valid UTF8

            --size;  // must decrement *after* (or Back_Scan() will fail)
            if (size != 0)
                return Makehook_String(level_, REB_ISSUE, arg);
        }
        Option(Error*) error = Trap_Init_Char(OUT, c);
        if (error)
            return RAISE(unwrap error);
        return OUT; }

      default:
        break;
    }

  bad_make:

    return RAISE(Error_Bad_Make(k, arg));
}


//
//  /codepoint-to-char: native [
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

    Option(Error*) error = Trap_Init_Char(OUT, c);
    if (error)
        return FAIL(unwrap error);
    return OUT;
}


//
//  /utf8-to-char: native [
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
        return FAIL("Empty binary passed to UTF8-TO-CHAR");

    Codepoint c;
    Option(Error*) e = Trap_Back_Scan_Utf8_Char(&c, &encoded, &size);
    if (e)
        return FAIL(unwrap e);

    assert(size != 0);  // Back_Scan() assumes one byte decrement

    if (size != 1)
        return FAIL("More than one codepoint found in UTF8-TO-CHAR conversion");

    Init_Char_Unchecked(OUT, c);  // !!! Guaranteed good character?
    return OUT;
}


static REBINT Math_Arg_For_Char(Value* arg, const Symbol* verb)
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
void MF_Sigil(Molder* mo, const Cell* v, bool form)
{
    UNUSED(form);
    Append_Any_Utf8(mo->string, v);
}


//
//  MF_Issue: C
//
void MF_Issue(Molder* mo, const Cell* v, bool form)
{
    if (form) {
        if (IS_CHAR_CELL(v) and Cell_Codepoint(v) == 0)
            fail (Error_Illegal_Zero_Byte_Raw());  // don't form #, only mold

        Append_Any_Utf8_Limit(mo->string, v, UNLIMITED);
        return;
    }

    Length len;
    Utf8(const*) cp = Cell_Utf8_Len_Size_At(&len, nullptr, v);

    Append_Codepoint(mo->string, '#');

    if (len == 0) {
        Append_Codepoint(mo->string, '"');
        Append_Codepoint(mo->string, '"');
        return;
    }

    bool no_quotes = true;
    Codepoint c = Codepoint_At(cp);

    if (len == 1 and c == ' ')
        return;  // # is notationally a space character

    // !!! This should be smarter and share code with FILE! on whether
    // it's necessary to use double quotes or braces, and how escaping
    // should be done.  For now, just do a simple scan to get the gist
    // of what that logic *should* do.

    for (; c != '\0'; cp = Utf8_Next(&c, cp)) {
        if (c > UINT8_MAX)
            continue;
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
        if (Is_Lex_Delimit(cast(Byte, c))) {
            no_quotes = false;  // comma, bracket, parentheses, quotes...
            break;
        }
    }

    if (no_quotes or not Stringlike_Has_Node(v)) {  // !!! hack
        if (len == 1 and not no_quotes) {  // use historical CHAR! molding
            bool parened = true;  // !!! used to depend on MOLD's :ALL flag

            Append_Codepoint(mo->string, '"');
            Mold_Codepoint(mo, Cell_Codepoint(v), parened);
            Append_Codepoint(mo->string, '"');
        }
        else
            Append_Any_Utf8_Limit(mo->string, v, &len);
    }
    else {
        const String* s = Cell_String(v);  // !!! needs node
        Mold_Text_Flex_At(mo, s, 0);
    }
}


//
//  REBTYPE: C
//
REBTYPE(Utf8)
{
    Option(SymId) id = Symbol_Id(verb);

    Value* issue = D_ARG(1);

    switch (id) {
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
        return FAIL(PARAM(property)); }

      case SYM_COPY:  // since result is also immutable, Copy_Cell() suffices
        return Copy_Cell(OUT, issue);

  //=//// TO CONVERSIONS //////////////////////////////////////////////////=//

      case SYM_TO_P:
        return T_String(level_, verb);  // written to handle non-node cases

      default:
        break;
    }

    if (Stringlike_Has_Node(issue)) {
        assert(not IS_CHAR(issue));  // no string math
        return T_String(level_, verb);
    }

    switch (id) {
      case SYM_PICK_P: {
        INCLUDE_PARAMS_OF_PICK_P;
        UNUSED(ARG(location));

        const Value* picker = ARG(picker);
        if (not Is_Integer(picker))
            return FAIL(PARAM(picker));

        REBI64 n = VAL_INT64(picker);
        if (n <= 0)
            return RAISE(Error_Bad_Pick_Raw(picker));

        REBLEN len;
        Utf8(const*) cp = Cell_Utf8_Len_Size_At(&len, nullptr, issue);
        if (n > len)
            return nullptr;

        Codepoint c;
        cp = Utf8_Next(&c, cp);
        for (; n != 1; --n)
            cp = Utf8_Next(&c, cp);

        return Init_Integer(OUT, c); }

      default:
        break;
    }

    // !!! All the math operations below are inherited from the CHAR!
    // implementation, and will not work if the ISSUE! length is > 1.
    //
    if (not IS_CHAR(issue))
        return FAIL("Math operations only usable on single-character ISSUE!");

    // Don't use a Codepoint for chr, because it does signed math and then will
    // detect overflow.
    //
    REBI64 chr = cast(REBI64, Cell_Codepoint(issue));
    REBI64 arg;

    switch (id) {
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
            return FAIL(Error_Zero_Divide_Raw());
        chr /= arg;
        break;

      case SYM_REMAINDER:
        arg = Math_Arg_For_Char(D_ARG(2), verb);
        if (arg == 0)
            return FAIL(Error_Zero_Divide_Raw());
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
            return FAIL(Error_Bad_Refines_Raw());

        if (REF(seed)) {
            Set_Random(chr);
            return NOTHING;
        }
        if (chr == 0)
            break;
        chr = cast(Codepoint, 1 + (Random_Int(REF(secure)) % chr));
        break; }

      default:
        return UNHANDLED;
    }

    if (chr < 0)
        return RAISE(Error_Type_Limit_Raw(Datatype_From_Kind(REB_ISSUE)));

    Option(Error*) error = Trap_Init_Char(OUT, cast(Codepoint, chr));
    if (error)
        return RAISE(unwrap error);
    return OUT;
}


//
//  /trailing-bytes-for-utf8: native [
//
//  "Given the first byte of a UTF-8 encoding, how many bytes should follow"
//
//      return: [integer!]
//      first-byte [integer!]
//      :extended "Permit 4 or 5 trailing bytes, not legal in the UTF-8 spec"
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
        return FAIL(Error_Out_Of_Range(ARG(first_byte)));

    uint_fast8_t trail = g_trailing_bytes_for_utf8[cast(Byte, byte)];
    if (trail > 3 and not REF(extended)) {
        assert(trail == 4 or trail == 5);
        return FAIL(
            "Use :EXTENDED with TRAILING-BYTES-FOR-UTF-8 for 4 or 5 bytes"
        );
    }

    return Init_Integer(OUT, trail);
}
