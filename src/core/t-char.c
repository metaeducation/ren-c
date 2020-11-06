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
const char trailingBytesForUTF8[256] = {
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
const uint_fast32_t offsetsFromUTF8[6] = {
    0x00000000UL, 0x00003080UL, 0x000E2080UL,
    0x03C82080UL, 0xFA082080UL, 0x82082080UL
};


// Once the bits are split out into bytes of UTF-8, this is a mask OR-ed
// into the first byte, depending on how many bytes follow.  There are
// as many entries in this table as there are UTF-8 sequence types.
// (I.e., one byte sequence, two byte... etc.). Remember that sequencs
// for *legal* UTF-8 will be 4 or fewer bytes total.
//
const uint_fast8_t firstByteMark[7] = {
    0x00, 0x00, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC
};


//
//  CT_Issue: C
//
// As the replacement for CHAR!, ISSUE! inherits the behavior that there are
// no non-strict comparisons.  To compare non-strictly, they must be aliased
// as TEXT!.
//
REBINT CT_Issue(REBCEL(const*) a, REBCEL(const*) b, bool strict)
{
    UNUSED(strict);  // always strict

    if (IS_CHAR_CELL(a) and IS_CHAR_CELL(b)) {
        REBINT num = VAL_CHAR(a) - VAL_CHAR(b);
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
REB_R MAKE_Issue(
    REBVAL *out,
    enum Reb_Kind kind,
    const REBVAL *opt_parent,
    const REBVAL *arg
){
    assert(kind == REB_ISSUE);
    if (opt_parent)
        fail (Error_Bad_Make_Parent(kind, opt_parent));

    switch(VAL_TYPE(arg)) {
      case REB_INTEGER:
      case REB_DECIMAL: {
        REBINT n = Int32(arg);
        return Init_Char_May_Fail(out, n); }

      case REB_BINARY: {
        REBSIZ size;
        const REBYTE *bp = VAL_BINARY_SIZE_AT(&size, arg);
        if (size == 0)
            goto bad_make;

        REBUNI c;
        if (*bp <= 0x80) {
            if (size != 1)
                return MAKE_String(out, kind, nullptr, arg);

            c = *bp;
        }
        else {
            bp = Back_Scan_UTF8_Char(&c, bp, &size);
            --size;  // must decrement *after* (or Back_Scan() will fail)
            if (bp == nullptr)
                goto bad_make;  // must be valid UTF8
            if (size != 0)
                return MAKE_String(out, kind, nullptr, arg);
        }
        return Init_Char_May_Fail(out, c); }

      case REB_TEXT:
        if (VAL_LEN_AT(arg) == 0)
            fail ("Empty ISSUE! is zero codepoint, unlike empty TEXT!");
        if (VAL_LEN_AT(arg) == 1)
            return Init_Char_Unchecked(out, CHR_CODE(VAL_UTF8_AT(arg)));
        return MAKE_String(out, kind, nullptr, arg);

      default:
        break;
    }

  bad_make:
    fail (Error_Bad_Make(REB_ISSUE, arg));
}


//
//  TO_Issue: C
//
// !!! We want `to char! 'x` to give #"x" back.  But `make char! "&nbsp;"`
// might be best acting like #"&" ?  Consider in light of a general review
// of the sematnics of MAKE and TO.
//
REB_R TO_Issue(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    assert(VAL_TYPE(arg) != REB_ISSUE);  // !!! should call COPY?

    if (ANY_STRING(arg) or ANY_WORD(arg)) {
        REBLEN len;
        REBSIZ size;
        REBCHR(const*) utf8 = VAL_UTF8_LEN_SIZE_AT(&len, &size, arg);

        if (len == 0)  // don't "accidentally" create zero-codepoint `#`
            fail (Error_Illegal_Zero_Byte_Raw());

        return Init_Issue_Utf8(out, utf8, size, len);
    }

    fail (Error_Bad_Cast_Raw(arg, Datatype_From_Kind(kind)));
}


static REBINT Math_Arg_For_Char(REBVAL *arg, const REBVAL *verb)
{
    switch (VAL_TYPE(arg)) {
    case REB_ISSUE:
        return VAL_CHAR(arg);

    case REB_INTEGER:
        return VAL_INT32(arg);

    case REB_DECIMAL:
        return cast(REBINT, VAL_DECIMAL(arg));

    default:
        fail (Error_Math_Args(REB_ISSUE, verb));
    }
}


//
//  MF_Issue: C
//
void MF_Issue(REB_MOLD *mo, REBCEL(const*) v, bool form)
{
    enum Reb_Kind heart = CELL_HEART(v);
    REBLEN len;
    if (heart == REB_BYTES)
        len = EXTRA(Bytes, v).exactly_4[IDX_EXTRA_LEN];
    else {
        assert(heart == REB_TEXT);
        len = VAL_LEN_AT(v);
    }

    if (form) {
        if (IS_CHAR(v) and VAL_CHAR(v) == 0)
            fail (Error_Illegal_Zero_Byte_Raw());  // don't form #, only mold

        Append_String_Limit(mo->series, v, len);
        return;
    }

    Append_Codepoint(mo->series, '#');

    if (len == 0)
        return;  // Just be `#`

    // !!! This should be smarter and share code with FILE! on whether
    // it's necessary to use double quotes or braces, and how escaping
    // should be done.  For now, just do a simple scan to get the gist
    // of what that logic *should* do.

    bool no_quotes = true;
    REBCHR(const*) cp = VAL_UTF8_AT(v);
    REBUNI c = CHR_CODE(cp);
    for (; c != '\0'; cp = NEXT_CHR(&c, cp)) {
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

    if (no_quotes or heart == REB_BYTES) {  // !!! hack
        if (len == 1 and not no_quotes) {  // use historical CHAR! path
            bool parened = GET_MOLD_FLAG(mo, MOLD_FLAG_ALL);

            Append_Codepoint(mo->series, '"');
            Mold_Uni_Char(mo, VAL_CHAR(v), parened);
            Append_Codepoint(mo->series, '"');
        }
        else
            Append_String_Limit(mo->series, v, len);
    } else
        Mold_Text_Series_At(mo, VAL_STRING(v), 0);
}


//
//  PD_Issue: C
//
// It's not clear if allowing picking of codepoints as integers is a good or
// bad idea for ISSUE!.  But add it in just to try.
//
REB_R PD_Issue(
    REBPVS *pvs,
    const RELVAL *picker,
    const REBVAL *opt_setval
){
    if (opt_setval)
        fail ("ISSUE! is immutable, characters can't assign via SET-PATH!");

    if (not IS_INTEGER(picker))
        return R_UNHANDLED;

    REBI64 n = VAL_INT64(picker);
    if (n <= 0)
        return nullptr;

    REBLEN len;
    REBCHR(const*) cp = VAL_UTF8_LEN_SIZE_AT(&len, nullptr, pvs->out);
    if (cast(REBLEN, n) > len)
        return nullptr;

    REBUNI c;
    cp = NEXT_CHR(&c, cp);
    for (; n != 1; --n)
        cp = NEXT_CHR(&c, cp);

    return Init_Integer(pvs->out, c);
}


//
//  REBTYPE: C
//
REBTYPE(Issue)
{
    REBVAL *issue = D_ARG(1);

    REBSYM sym = VAL_WORD_SYM(verb);

    switch (sym) {
      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;
        UNUSED(ARG(value));  // same as `v`

        switch (VAL_WORD_SYM(ARG(property))) {
          case SYM_CODEPOINT:
            if (not IS_CHAR(issue))
                break;  // must be a single codepoint to use this reflector
            return Init_Integer(D_OUT, VAL_CHAR(issue));

          case SYM_SIZE: {
            REBSIZ size;
            VAL_UTF8_SIZE_AT(&size, issue);
            return Init_Integer(D_OUT, size); }

          case SYM_LENGTH: {
            REBLEN len;
            VAL_UTF8_LEN_SIZE_AT(&len, nullptr, issue);
            return Init_Integer(D_OUT, len); }

          default:
            break;
        }
        return R_UNHANDLED; }

      case SYM_COPY:  // since copy result is also immutable, Move() suffices
        return Move_Value(D_OUT, issue);

      default:
        break;
    }

    // !!! All the math operations below are inherited from the CHAR!
    // implementation, and will not work if the ISSUE! length is > 1.
    //
    if (not IS_CHAR(issue))
        return R_UNHANDLED;

    // Don't use a REBUNI for chr, because it does signed math and then will
    // detect overflow.
    //
    REBI64 chr = cast(REBI64, VAL_CHAR(issue));
    REBI64 arg;

    switch (sym) {
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
            Init_Integer(D_OUT, chr - arg);
            return D_OUT;
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
        chr = cast(REBUNI, ~chr);
        break;

      case SYM_BITWISE_AND:
        arg = Math_Arg_For_Char(D_ARG(2), verb);
        chr &= cast(REBUNI, arg);
        break;

      case SYM_BITWISE_OR:
        arg = Math_Arg_For_Char(D_ARG(2), verb);
        chr |= cast(REBUNI, arg);
        break;

      case SYM_BITWISE_XOR:
        arg = Math_Arg_For_Char(D_ARG(2), verb);
        chr ^= cast(REBUNI, arg);
        break;

      case SYM_BITWISE_AND_NOT:
        arg = Math_Arg_For_Char(D_ARG(2), verb);
        chr &= cast(REBUNI, ~arg);
        break;

      case SYM_EVEN_Q:
        return Init_Logic(D_OUT, did (cast(REBUNI, ~chr) & 1));

      case SYM_ODD_Q:
        return Init_Logic(D_OUT, did (chr & 1));

      case SYM_RANDOM: {
        INCLUDE_PARAMS_OF_RANDOM;

        UNUSED(PAR(value));
        if (REF(only))
            fail (Error_Bad_Refines_Raw());

        if (REF(seed)) {
            Set_Random(chr);
            return nullptr;
        }
        if (chr == 0)
            break;
        chr = cast(REBUNI,
            1 + cast(REBLEN, Random_Int(did REF(secure)) % chr)
        );
        break; }

      default:
        return R_UNHANDLED;
    }

    if (chr < 0)
        fail (Error_Type_Limit_Raw(Datatype_From_Kind(REB_ISSUE)));

    return Init_Char_May_Fail(D_OUT, cast(REBUNI, chr));
}


//
//  trailing-bytes-for-utf8: native [
//
//  {Given the first byte of a UTF-8 encoding, how many bytes should follow}
//
//      return: [integer!]
//      first-byte [integer!]
//      /extended "Permit 4 or 5 trailing bytes, not legal in the UTF-8 spec"
//  ]
//
REBNATIVE(trailing_bytes_for_utf8)
//
// !!! This is knowledge Rebol has, and it can be useful for anyone writing
// code that processes UTF-8 (e.g. the terminal).  Might as well expose it.
{
    INCLUDE_PARAMS_OF_TRAILING_BYTES_FOR_UTF8;

    REBINT byte = VAL_INT32(ARG(first_byte));
    if (byte < 0 or byte > 255)
        fail (Error_Out_Of_Range(ARG(first_byte)));

    uint_fast8_t trail = trailingBytesForUTF8[cast(REBYTE, byte)];
    if (trail > 3 and not REF(extended)) {
        assert(trail == 4 or trail == 5);
        fail ("Use /EXTENDED with TRAILNG-BYTES-FOR-UTF-8 for 4 or 5 bytes");
    }

    return Init_Integer(D_OUT, trail);
}

